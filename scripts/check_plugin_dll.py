#!/usr/bin/env python3
"""校验一个 DLL 是不是「SKSE 会认」的插件，并报告能否符号化。

不需要任何第三方库，只解析 PE 头/导出表/调试目录。

用法:
    python check_plugin_dll.py dist/SKYRIM/Plugins/X.dll
    python check_plugin_dll.py X.dll --json

退出码: 0 = 能当插件用（可能带警告）; 1 = 一定不会被 SKSE 加载
"""
from __future__ import annotations

import json
import os
import struct
import sys

MACHINE = {0x8664: "x64", 0x14C: "x86", 0xAA64: "arm64"}


def u16(b, o):
    return struct.unpack_from("<H", b, o)[0]


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


class PE:
    def __init__(self, data: bytes):
        self.d = data
        if data[:2] != b"MZ":
            raise ValueError("不是 PE 文件（缺 MZ）")
        self.nt = u32(data, 0x3C)
        if data[self.nt:self.nt + 4] != b"PE\0\0":
            raise ValueError("不是 PE 文件（缺 PE 签名）")
        fh = self.nt + 4
        self.machine = u16(data, fh)
        self.nsec = u16(data, fh + 2)
        self.timestamp = u32(data, fh + 4)
        self.opt_size = u16(data, fh + 16)
        self.chars = u16(data, fh + 18)
        self.opt = fh + 20
        self.magic = u16(data, self.opt)
        self.pe32plus = self.magic == 0x20B
        dd_off = self.opt + (112 if self.pe32plus else 96)
        self.dd_count = u32(data, dd_off - 4)
        self.dirs = []
        for i in range(min(self.dd_count, 16)):
            self.dirs.append((u32(data, dd_off + i * 8), u32(data, dd_off + i * 8 + 4)))
        sec = self.opt + self.opt_size
        self.sections = []
        for i in range(self.nsec):
            o = sec + i * 40
            name = data[o:o + 8].rstrip(b"\0").decode("latin-1")
            vsize = u32(data, o + 8)
            va = u32(data, o + 12)
            rsize = u32(data, o + 16)
            rptr = u32(data, o + 20)
            self.sections.append((name, va, max(vsize, 1), rptr, rsize))

    def off(self, rva: int):
        for _n, va, vsize, rptr, rsize in self.sections:
            if va <= rva < va + max(vsize, rsize):
                delta = rva - va
                if delta < rsize:
                    return rptr + delta
                return None
        return None

    def exports(self):
        if not self.dirs or self.dirs[0][0] == 0:
            return []
        o = self.off(self.dirs[0][0])
        if o is None:
            return []
        n_names = u32(self.d, o + 24)
        names_rva = u32(self.d, o + 32)
        no = self.off(names_rva)
        if no is None:
            return []
        out = []
        for i in range(n_names):
            rva = u32(self.d, no + i * 4)
            p = self.off(rva)
            if p is None:
                continue
            end = self.d.find(b"\0", p)
            out.append(self.d[p:end].decode("latin-1"))
        return out

    def has_codeview(self):
        """调试目录里有没有 CodeView(RSDS) —— 没有就永远配不上 .pdb。"""
        if len(self.dirs) <= 6 or self.dirs[6][0] == 0:
            return False
        o = self.off(self.dirs[6][0])
        if o is None:
            return False
        size = self.dirs[6][1]
        for i in range(0, max(size, 28) // 28):
            e = o + i * 28
            if e + 28 > len(self.d):
                break
            if u32(self.d, e + 12) == 2:      # IMAGE_DEBUG_TYPE_CODEVIEW
                return True
        return False


def check(path: str):
    warns, errs = [], []
    if not os.path.isfile(path):
        return {"path": path, "ok": False, "errors": ["文件不存在"], "warnings": []}
    data = open(path, "rb").read()
    try:
        pe = PE(data)
    except ValueError as e:
        return {"path": path, "ok": False, "errors": [str(e)], "warnings": []}

    arch = MACHINE.get(pe.machine, hex(pe.machine))
    if arch != "x64":
        errs.append(f"不是 x64（{arch}）—— SKSE SE/AE 只加载 64 位 DLL")

    is_dll = bool(pe.chars & 0x2000)
    if not is_dll:
        errs.append("PE 头没有 DLL 标志（Characteristics 0x2000 未置位）")

    exps = pe.exports()
    if "SKSEPlugin_Load" not in exps:
        errs.append("导出表缺 SKSEPlugin_Load —— SKSE 会静默忽略这个 DLL")
    if "SKSEPlugin_Query" not in exps and "SKSEPlugin_Version" not in exps:
        warns.append("既没有 SKSEPlugin_Query 也没有 SKSEPlugin_Version"
                     "（NG 插件通常不需要，但老式插件缺了会不加载）")

    cv = pe.has_codeview()
    if not cv:
        warns.append("没有 CodeView(RSDS) —— 这个 DLL 的崩溃地址无法符号化，"
                     "需 CMake 加 /Zi /FS + /DEBUG:FULL 后重建")

    blob = data.lower()
    notes = []
    if b"versionlib" in blob:
        # NG 插件几乎都会引用，属正常，不作为警告
        notes.append("引用了 versionlib（Address Library 运行期解析）")

    ok = not errs
    return {"path": path, "ok": ok, "arch": arch, "size": len(data),
            "exports_skse": [e for e in exps if e.lower().startswith("skseplugin")],
            "codeview": cv, "errors": errs, "warnings": warns, "notes": notes}


def main(argv):
    as_json = "--json" in argv
    files = [a for a in argv[1:] if not a.startswith("-")]
    if not files:
        print(__doc__)
        return 2
    results = [check(f) for f in files]
    if as_json:
        print(json.dumps(results, ensure_ascii=False, indent=1))
    else:
        for r in results:
            name = os.path.basename(r["path"])
            print(f"\n=== {name} ===")
            if "size" in r:
                print(f"  架构 {r['arch']}   大小 {r['size']:,} 字节   "
                      f"CodeView {'有' if r['codeview'] else '无'}")
                print(f"  SKSE 导出: {', '.join(r['exports_skse']) or '(无)'}")
            for e in r["errors"]:
                print(f"  [错误] {e}")
            for w in r["warnings"]:
                print(f"  [警告] {w}")
            for n in r.get("notes", []):
                print(f"  [信息] {n}")
            print("  结论:", "可用" if r["ok"] else "不可用")
    return 0 if all(r["ok"] for r in results) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
