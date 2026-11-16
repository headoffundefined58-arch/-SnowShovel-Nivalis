"""把 SnowShovel 源码整理成一个可推送的 git 仓库（SE/AE 云端构建用）。

本机没有 MSVC，SE 版 DLL 只能靠 GitHub Actions 出。这个脚本负责：
  1. git init（若尚未初始化）
  2. 写 .gitignore，排除 build 产物与 PDB（PDB 有 17MB，不必上传）
  3. git add + commit（身份用本地 config，不动全局）

它不会 push —— push 需要你的 GitHub 写权限，见下方用法。

用法：
  python prepare_push.py                      # 只 init + commit
  python prepare_push.py --remote <仓库URL>    # 顺带设置 origin

之后手动 push（走你自己的凭证）：
  git -C D:/dev/SnowShovel push -u origin main

仓库要求：新建一个空仓库（public/private 均可）。push 后 push 事件会触发
.github/workflows/build.yml，构建出 Nivalis-SE-1-5-97 与 Nivalis-AE-1-6-x
两个 artifact，到 Actions 页面下载即可。
"""
import os
import subprocess
import sys

ROOT = r"D:\dev\SnowShovel"
REMOTE = None
if "--remote" in sys.argv:
    i = sys.argv.index("--remote")
    if i + 1 < len(sys.argv):
        REMOTE = sys.argv[i + 1]

GITIGNORE = """# 构建产物（含 17MB 的 PDB，不上传）
build2/
build/
build-se/
dist/
*.pdb
*.ilk
*.obj
*.tlog
*.idb
*.exp
*.exe
*.obj

# Python / 编辑器
__pycache__/
*.pyc
.vs/
.vscode/
.idea/
*.user

# OS
Thumbs.db
desktop.ini
.DS_Store
"""

# 本地提交身份（不写全局，避免影响其他仓库）
LOCAL_USER = "SnowShovel Builder"
LOCAL_EMAIL = "snowshovel-builder@users.noreply.github.com"


def run(cmd, check=True):
    print(f"$ {' '.join(cmd)}", flush=True)
    p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True,
                       encoding="utf-8", errors="replace")
    if p.stdout:
        print(p.stdout.rstrip(), flush=True)
    if p.stderr:
        print(p.stderr.rstrip(), flush=True)
    if check and p.returncode != 0:
        raise SystemExit(f"命令失败 (exit {p.returncode}): {cmd[0]}")
    return p.returncode == 0


def main():
    if not os.path.isdir(ROOT):
        raise SystemExit(f"项目目录不存在: {ROOT}")

    # 1. git init（幂等）
    if not os.path.isdir(os.path.join(ROOT, ".git")):
        run(["git", "init", "-b", "main"])
    else:
        print("git 仓库已存在，跳过 init", flush=True)

    # 2. .gitignore
    gi = os.path.join(ROOT, ".gitignore")
    with open(gi, "w", encoding="utf-8", newline="\n") as f:
        f.write(GITIGNORE)
    print(f"写入 {gi}", flush=True)

    # 3. 本地身份
    run(["git", "config", "user.name", LOCAL_USER])
    run(["git", "config", "user.email", LOCAL_EMAIL])

    # 4. remote（可选）
    if REMOTE:
        run(["git", "remote", "remove", "origin"], check=False)
        run(["git", "remote", "add", "origin", REMOTE])

    # 5. add + commit
    run(["git", "add", "-A"])
    # 列出将提交的文件数，供确认
    p = subprocess.run(["git", "diff", "--cached", "--name-only"],
                       cwd=ROOT, capture_output=True, text=True,
                       encoding="utf-8", errors="replace")
    files = [x for x in p.stdout.splitlines() if x.strip()]
    print(f"\n暂存文件数: {len(files)}", flush=True)
    print("前 15 个:", flush=True)
    for f in files[:15]:
        print("  ", f, flush=True)
    if len(files) > 15:
        print(f"   ... 还有 {len(files) - 15} 个", flush=True)

    if not files:
        print("没有需要提交的改动", flush=True)
        return 0

    commit = subprocess.run(
        ["git", "commit", "-m", "SE/AE dual-runtime cloud build (Nivalis 0.5.0)"],
        cwd=ROOT, capture_output=True, text=True,
        encoding="utf-8", errors="replace")
    if commit.stdout:
        print(commit.stdout.rstrip(), flush=True)
    if commit.stderr:
        print(commit.stderr.rstrip(), flush=True)
    if commit.returncode != 0:
        raise SystemExit(f"commit 失败 (exit {commit.returncode})")

    print("\n=== 下一步 ===", flush=True)
    if REMOTE:
        print("  git -C D:/dev/SnowShovel push -u origin main", flush=True)
    else:
        print("  先在 GitHub 新建一个空仓库，然后：", flush=True)
        print("  python prepare_push.py --remote <仓库URL>", flush=True)
        print("  git -C D:/dev/SnowShovel push -u origin main", flush=True)
    print("  push 后到 Actions 页面下载 SE / AE 两个 artifact", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
