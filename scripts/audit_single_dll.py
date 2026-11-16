#!/usr/bin/env python3
"""判断「一份 DLL 同时支持 SE+AE」对当前代码是否安全。

背景（对 CommonLibSSE-NG 源码核对过）：
库里有 6 个类用 #ifdef SKYRIM_SUPPORT_AE 做编译期布局分叉，而该宏在整棵树里
没有任何地方定义。多运行时构建（SE+AE 都开）只能按 SE 布局编译，于是在 AE 上：

    TES                 worldSpace 在 0x140，AE 实际 0x148
    CombatController    缺 aimControllerLock，0xD8 vs 0xE0，其后全体后移
    ControlMap          gamePadMapType 等尾部成员，0x128 vs 0x130
    InterfaceStrings    缺 marketplaceMenu，其后所有 BSFixedString 后移 8
    UserEvents          kMarketplace 只在 AE，其后枚举值整体 +1
    BGSSaveLoadManager  两处成员分叉

只要代码走到这些成员上，单 DLL 在另一侧运行时就会读到垃圾（典型：崩溃在
`-> 20543+0x43`，this 是 44.0f 之类的浮点）。

用法:
    python audit_single_dll.py <源码目录> [更多目录...]
    python audit_single_dll.py src --strict     # 有任何 WARN 也判不通过
    python audit_single_dll.py src --json

退出码: 0 = 可以出单 DLL; 1 = 必须双构建（或改成不碰这些成员）
"""
from __future__ import annotations

import json
import os
import re
import sys

EXTS = (".cpp", ".cc", ".cxx", ".h", ".hpp", ".hxx", ".inl")

# (正则, 级别, 说明)
CHECKS = [
    # ---- TES：worldSpace 与所有会走 sky cell 的接口
    (r"\bworldSpace\b", "ERROR", "TES::worldSpace 裸成员，SE 0x140 / AE 0x148，无访问器"),
    (r"\bForEachReferenceInRange\b", "ERROR", "内部走 GetSkyCell，会读 worldSpace"),
    (r"\bGetSkyCell\b", "ERROR", "同上"),
    (r"\bGetLandTexture\b", "ERROR", "TES 内部成员，受布局分叉影响"),
    (r"\bGetLandHeight\b", "ERROR", "同上"),
    (r"->\s*GetCell\s*\(", "ERROR", "TES::GetCell 走 sky cell 路径"),

    # ---- CombatController
    (r"\bCombatController\b", "ERROR", "整个类 0xD8/0xE0 分叉"),
    (r"\baimControllerLock\b", "ERROR", "只在 AE 存在的成员"),
    (r"\b(current|previous)AimController\b", "ERROR", "0x68 之后成员，AE 全体 +8"),
    (r"\b(current|previous)TargetSelector\b", "ERROR", "同上"),
    (r"\bcurrentArea\b", "ERROR", "同上"),

    # ---- ControlMap / InterfaceStrings / UserEvents / BGSSaveLoadManager
    (r"\bgamePadMapType\b", "ERROR", "ControlMap 尾部成员，0x128/0x130 分叉"),
    (r"\bmarketplaceMenu\b", "ERROR", "只在 AE 存在，其后 BSFixedString 全部后移 8"),
    (r"\bkMarketplace\b", "ERROR", "只在 AE 存在，其后枚举值 +1"),

    # ---- 直接对分叉类取尺寸（一定会错）
    (r"sizeof\s*\(\s*(TES|CombatController|ControlMap|InterfaceStrings|BGSSaveLoadManager)\s*\)",
     "ERROR", "这些类型 SE/AE 尺寸不同，不能用 sizeof"),

    # ---- 有风险但要看具体成员：先告警
    (r"\bInterfaceStrings\b", "WARN", "0x1A0 之后的字符串成员在 AE 上偏移 +8"),
    (r"\bUserEvents::", "WARN", "枚举值在 AE 上 kMarketplace 之后整体 +1"),
    (r"\bBGSSaveLoadManager\b", "WARN", "两处成员分叉"),
    (r"\bControlMap\b", "WARN", "尾部成员在 AE 上偏移不同"),
]

COMMENT_OR_STRING = re.compile(
    r"//[^\n]*|/\*.*?\*/|\"(?:\\.|[^\"\\])*\"|'(?:\\.|[^'\\])*'", re.S)


def strip(text: str) -> str:
    """去掉注释与字符串字面量，保留行号（用等长空格替换）。"""
    def repl(m):
        s = m.group(0)
        return "".join("\n" if c == "\n" else " " for c in s)
    return COMMENT_OR_STRING.sub(repl, text)


def scan(dirs):
    hits = []
    files = 0
    for root_dir in dirs:
        if os.path.isfile(root_dir):
            walk = [(os.path.dirname(root_dir), [], [os.path.basename(root_dir)])]
        else:
            walk = os.walk(root_dir)
        for base, _sub, names in walk:
            for n in names:
                if not n.lower().endswith(EXTS):
                    continue
                p = os.path.join(base, n)
                files += 1
                try:
                    raw = open(p, "r", encoding="utf-8", errors="replace").read()
                except OSError:
                    continue
                code = strip(raw)
                for pat, level, why in CHECKS:
                    for m in re.finditer(pat, code):
                        line = code.count("\n", 0, m.start()) + 1
                        try:
                            shown = os.path.relpath(p)
                        except ValueError:
                            shown = p          # 跨盘符时 relpath 会抛，直接用绝对路径
                        hits.append({
                            "file": shown, "line": line,
                            "level": level, "match": m.group(0).strip(),
                            "why": why,
                        })
    return files, hits


def main(argv):
    as_json = "--json" in argv
    strict = "--strict" in argv
    dirs = [a for a in argv[1:] if not a.startswith("-")]
    if not dirs:
        print(__doc__)
        return 2

    files, hits = scan(dirs)
    errors = [h for h in hits if h["level"] == "ERROR"]
    warns = [h for h in hits if h["level"] == "WARN"]
    blocked = bool(errors) or (strict and bool(warns))

    if as_json:
        print(json.dumps({"files": files, "errors": errors, "warnings": warns,
                          "safe_for_single_dll": not blocked},
                         ensure_ascii=False, indent=1))
    else:
        print(f"扫描 {files} 个源文件")
        for h in errors + warns:
            tag = "错误" if h["level"] == "ERROR" else "警告"
            print(f"  [{tag}] {h['file']}:{h['line']}  {h['match']}")
            print(f"         {h['why']}")
        print("-" * 60)
        if not blocked:
            print("结论: 可以出单 DLL（三个运行时全开、不定义 SKYRIM_SUPPORT_AE）")
            if warns and not strict:
                print("      注意有 WARN 项：只是没用 --strict 才放行，发版前人工确认。")
        else:
            print("结论: 不能出单 DLL —— 会踩到编译期布局分叉。")
            print("      两个选择：")
            print("        1) 出双份 DLL（AE 一份 / SE 一份），见 SKILL.md §3 的参数表；")
            print("        2) 把这些调用改成不依赖分叉成员（多数情况下做不到）。")
    return 0 if not blocked else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
