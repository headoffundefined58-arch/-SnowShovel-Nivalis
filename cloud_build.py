"""SnowShovel / Nivalis —— 一键云端构建（GitHub Actions）。

本机没有 MSVC 工具链，SE/AE 两个 DLL 只能靠 GitHub Actions 产出。
这个脚本把「建仓库 -> 推送 -> 等构建 -> 下载产物」串成一条命令。

用法：
    # 1) 准备 token（二选一）
    #    a. 环境变量 GITHUB_TOKEN / GH_TOKEN
    #    b. 同目录下放 .gh_token 文件，内容只有一行 token
    #    token 需要 repo（+ workflow）权限；classic PAT 或 fine-grained 均可。

    # 2) 直接跑
    python D:/dev/SnowShovel/cloud_build.py

    # 常用参数
    python cloud_build.py --name SnowShovel-Nivalis --public
    python cloud_build.py --repo myname/existing-repo      # 推到已有仓库，不新建
    python cloud_build.py --no-wait                        # 只推送，不等构建
    python cloud_build.py --timeout 60                     # 最多等 60 分钟

安全说明：token 只在本地内存与本脚本发起的 HTTPS 请求里使用，不写入仓库、
不写进 git remote（remote 用临时凭据注入，推完即恢复为干净 URL）。
用完后建议到 GitHub 撤销该 token。
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
import zipfile

ROOT = os.path.dirname(os.path.abspath(__file__))
API = "https://api.github.com"
TOKEN_FILE = os.path.join(ROOT, ".gh_token")
DEFAULT_NAME = "SnowShovel-Nivalis"
DIST = os.path.join(ROOT, "dist")
ARTIFACTS = os.path.join(DIST, "artifacts")

# 本机代理对 api.github.com 不稳定（502 Tunnel），统一直连。
_OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))


# ---------------------------------------------------------------- token / api

def load_token() -> str:
    for key in ("GITHUB_TOKEN", "GH_TOKEN", "GITHUB_PAT"):
        v = (os.environ.get(key) or "").strip()
        if v:
            return v
    if os.path.isfile(TOKEN_FILE):
        with open(TOKEN_FILE, encoding="utf-8-sig") as f:
            v = f.readline().strip()
            if v:
                return v
    # 兜底：向本机 git 凭据助手要一份（不落盘、不打印）
    try:
        p = subprocess.run(
            ["git", "credential", "fill"],
            input="protocol=https\nhost=github.com\n\n",
            capture_output=True, text=True, timeout=60,
            encoding="utf-8", errors="replace")
        for line in (p.stdout or "").splitlines():
            if line.startswith("password=") and len(line) > 20:
                return line[len("password="):].strip()
    except Exception:
        pass
    die(
        "找不到 token。请任选其一：\n"
        "  1) 设置环境变量 GITHUB_TOKEN\n"
        f"  2) 在 {TOKEN_FILE} 里写一行 token\n"
        "  3) 让本机 git 已登录 GitHub（脚本会用 git credential fill 取）\n"
        "token 需要 repo（以及 workflow）写权限。"
    )


def die(msg: str, code: int = 2) -> None:
    print(f"\n[错误] {msg}", file=sys.stderr)
    raise SystemExit(code)


def api(method: str, path: str, token: str, payload=None, allow=(200, 201, 204)):
    url = path if path.startswith("http") else API + path
    data = None
    headers = {
        "Authorization": f"Bearer {token}",
        "Accept": "application/vnd.github+json",
        "X-GitHub-Api-Version": "2022-11-28",
        "User-Agent": "snowshovel-cloud-build",
    }
    if payload is not None:
        data = json.dumps(payload).encode("utf-8")
        headers["Content-Type"] = "application/json"
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    try:
        with _OPENER.open(req, timeout=60) as r:
            body = r.read()
            status = r.status
    except urllib.error.HTTPError as e:
        body = e.read()
        status = e.code
    if status not in allow:
        snippet = body[:400].decode("utf-8", "replace")
        die(f"{method} {path} -> HTTP {status}\n{snippet}")
    if not body:
        return None
    try:
        return json.loads(body)
    except json.JSONDecodeError:
        return None


# ---------------------------------------------------------------------- git

def run(cmd, cwd=ROOT, check=True, quiet=False):
    env = {k: v for k, v in os.environ.items()
           if k.lower() not in ("http_proxy", "https_proxy", "all_proxy")}
    p = subprocess.run(cmd, cwd=cwd, env=env,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                       encoding="utf-8", errors="replace")
    if not quiet and p.stdout.strip():
        print(p.stdout.strip())
    if check and p.returncode != 0:
        die(f"命令失败 ({p.returncode}): {' '.join(cmd)}\n{p.stdout[-1500:]}")
    return p


def git(*args, **kw):
    return run(["git"] + list(args), **kw)


def ensure_git_identity():
    if not run(["git", "config", "--global", "user.name"], check=False, quiet=True).stdout.strip():
        git("config", "--global", "user.name", "SnowShovel Builder")
    if not run(["git", "config", "--global", "user.email"], check=False, quiet=True).stdout.strip():
        git("config", "--global", "user.email", "builder@localhost")


def ensure_repo_initialized():
    if not os.path.isdir(os.path.join(ROOT, ".git")):
        print("==> 初始化 git 仓库")
        git("init", "-b", "main")
    ensure_git_identity()
    st = run(["git", "status", "--porcelain"], quiet=True).stdout.strip()
    if st:
        print("==> 提交未跟踪的改动")
        git("add", "-A")
        git("commit", "-m", "chore: cloud build snapshot")
    # 确保有 main 分支
    br = run(["git", "rev-parse", "--abbrev-ref", "HEAD"], quiet=True).stdout.strip()
    if br != "main":
        git("branch", "-M", "main")


# --------------------------------------------------------------------- steps

def ensure_remote_repo(token: str, owner: str, name: str, private: bool) -> str:
    full = f"{owner}/{name}"
    print(f"==> 检查仓库 {full}")
    url = f"/repos/{full}"
    # 404 时 api() 返回 None（已在 allow 内），用它判定是否需要新建
    probe = api("GET", url, token, allow=(200, 404))
    if not probe:
        print(f"==> 创建{'私有' if private else '公开'}仓库 {full}")
        payload = {
            "name": name,
            "description": "Nivalis SKSE plugin - SE 1.5.97 + AE dual-runtime cloud build",
            "private": private,
            "auto_init": False,
            "has_issues": False,
            "has_wiki": False,
        }
        api("POST", "/user/repos", token, payload)
        for _ in range(10):
            time.sleep(1.5)
            probe = api("GET", url, token, allow=(200, 404))
            if probe:
                break
    else:
        print("==> 仓库已存在，直接推送")
    return full


def push_with_token(token: str, full: str) -> None:
    branch = "main"
    clean_url = f"https://github.com/{full}.git"
    auth_url = f"https://x-access-token:{urllib.parse.quote(token, safe='')}@github.com/{full}.git"
    # remote 只存干净 URL，推送时用 -c http.extraheader 注入凭据（不落盘）
    git("remote", "remove", "origin", check=False, quiet=True)
    git("remote", "add", "origin", clean_url)
    print(f"==> 推送 {branch} -> {full}")
    b64 = __import__("base64").b64encode(
        f"x-access-token:{token}".encode()).decode()
    p = run(["git", "-c", f"http.https://github.com/.extraheader=Authorization: Basic {b64}",
             "push", "-u", "origin", f"{branch}:{branch}", "--force"],
            check=False)
    if p.returncode != 0:
        # 兜底：直接带凭据的 URL 推一次
        print("==> 首次推送失败，改用带凭据 URL 重试")
        run(["git", "push", auth_url, f"{branch}:{branch}", "--force"])


def find_run(token: str, full: str, sha: str):
    runs = api("GET",
               f"/repos/{full}/actions/runs?branch=main&per_page=20",
               token, allow=(200, 404)) or {}
    for r in runs.get("workflow_runs", []):
        if r.get("head_sha") == sha:
            return r
    items = runs.get("workflow_runs", [])
    return items[0] if items else None


def wait_build(token: str, full: str, sha: str, timeout_min: int) -> dict:
    print("==> 等待 Actions 构建")
    deadline = time.time() + timeout_min * 60
    last = None
    run = None
    while time.time() < deadline:
        run = find_run(token, full, sha)
        if run:
            status, concl = run.get("status"), run.get("conclusion")
            line = f"    [{status}] {run.get('name')} #{run.get('run_number')}"
            if line != last:
                print(line)
                last = line
            if status == "completed":
                return run
        time.sleep(15)
    die(f"等待超时（{timeout_min} 分钟）。可到 "
        f"https://github.com/{full}/actions 手动查看。", 3)


def show_jobs(token: str, full: str, run_id: int) -> None:
    jobs = api("GET", f"/repos/{full}/actions/runs/{run_id}/jobs", token) or {}
    for j in jobs.get("jobs", []):
        print(f"    - {j.get('name')}: {j.get('conclusion') or j.get('status')}")
        if j.get("conclusion") == "failure":
            print(f"      日志: https://github.com/{full}/actions/runs/{run_id}")


def download_artifacts(token: str, full: str, run_id: int) -> list:
    os.makedirs(ARTIFACTS, exist_ok=True)
    listing = api("GET", f"/repos/{full}/actions/runs/{run_id}/artifacts", token) or {}
    arts = listing.get("artifacts", [])
    if not arts:
        print("==> 该次运行没有产物（可能构建失败）")
        return []
    saved = []
    for a in arts:
        name = re.sub(r'[^\w.\-]+', '_', a.get("name", "artifact"))
        zp = os.path.join(ARTIFACTS, name + ".zip")
        print(f"==> 下载产物 {a.get('name')} ({a.get('size_in_bytes', 0) / 1048576:.1f} MB)")
        download_zip(f"/repos/{full}/actions/artifacts/{a['id']}/zip", token, zp)
        outdir = os.path.join(DIST, name)
        if os.path.isdir(outdir):
            shutil.rmtree(outdir)
        os.makedirs(outdir, exist_ok=True)
        try:
            with zipfile.ZipFile(zp) as z:
                z.extractall(outdir)
        except zipfile.BadZipFile:
            print(f"    [警告] {zp} 不是有效 zip，可能下载被拦截")
            continue
        saved.append((outdir, zp))
    return saved


def download_zip(path: str, token: str, dest: str) -> None:
    """下载 artifact zip。GitHub 会 302 到签名 URL，需去掉 Authorization。"""
    class NoRedirect(urllib.request.HTTPRedirectHandler):
        def redirect_request(self, req, fp, code, msg, headers, newurl):
            return None

    opener = urllib.request.build_opener(NoRedirect, urllib.request.ProxyHandler({}))
    url = API + path
    req = urllib.request.Request(url, headers={
        "Authorization": f"Bearer {token}",
        "Accept": "application/vnd.github+json",
        "User-Agent": "snowshovel-cloud-build",
    })
    try:
        resp = opener.open(req, timeout=120)
        data = resp.read()
    except urllib.error.HTTPError as e:
        if e.code in (301, 302, 303, 307, 308):
            loc = e.headers.get("Location")
            if not loc:
                die(f"下载重定向缺少 Location: {path}")
            r2 = urllib.request.Request(loc, headers={"User-Agent": "snowshovel-cloud-build"})
            with _OPENER.open(r2, timeout=600) as resp:
                data = resp.read()
        else:
            die(f"下载失败 HTTP {e.code}: {path}\n{e.read()[:300].decode('utf-8', 'replace')}")
    with open(dest, "wb") as f:
        f.write(data)


# ---------------------------------------------------------------------- main

def main() -> None:
    ap = argparse.ArgumentParser(description="SnowShovel / Nivalis 一键云端构建")
    ap.add_argument("--repo", help="已有仓库 owner/name；省略则新建")
    ap.add_argument("--name", default=DEFAULT_NAME, help=f"新建仓库名（默认 {DEFAULT_NAME}）")
    ap.add_argument("--public", action="store_true", help="新建为公开仓库（默认私有）")
    ap.add_argument("--timeout", type=int, default=45, help="等待构建的分钟数（默认 45）")
    ap.add_argument("--no-wait", action="store_true", help="只推送，不等待构建")
    args = ap.parse_args()

    # 本机代理对 GitHub API 会 502，全程直连
    for k in list(os.environ):
        if k.lower() in ("http_proxy", "https_proxy", "all_proxy"):
            os.environ.pop(k, None)

    token = load_token()
    me = api("GET", "/user", token)
    owner = me["login"]
    print(f"==> 账号: {owner}")

    if args.repo:
        if "/" not in args.repo:
            die("--repo 需要 owner/name 形式")
        full = args.repo
        probe = api("GET", f"/repos/{full}", token, allow=(200, 404))
        if not probe:
            die(f"仓库 {full} 不存在或无权访问")
    else:
        full = ensure_remote_repo(token, owner, args.name, private=not args.public)

    ensure_repo_initialized()
    sha = run(["git", "rev-parse", "HEAD"], quiet=True).stdout.strip()
    print(f"==> 本地 HEAD: {sha[:8]}")
    push_with_token(token, full)

    if args.no_wait:
        print(f"\n已推送。构建页: https://github.com/{full}/actions")
        return

    # 给 GitHub 一点时间登记 run
    time.sleep(8)
    wf_run = wait_build(token, full, sha, args.timeout)
    run_id = wf_run["id"]
    print(f"==> 构建结束: {wf_run.get('conclusion')}  "
          f"https://github.com/{full}/actions/runs/{run_id}")
    show_jobs(token, full, run_id)

    if wf_run.get("conclusion") != "success":
        print("\n构建未成功，先看失败 job 的日志再重跑。", file=sys.stderr)
        raise SystemExit(4)

    saved = download_artifacts(token, full, run_id)
    print("\n==> 产物已下载：")
    for outdir, zp in saved:
        for dp, _, fs in os.walk(outdir):
            for f in fs:
                if f.lower().endswith((".dll", ".pdb", ".ini")):
                    print("   ", os.path.join(dp, f))
    print(f"\n目录: {DIST}")
    print("把对应 SKSE/Plugins 里的 DLL 发给支持者即可（SE 版 = SE-1-5-97）。")


if __name__ == "__main__":
    main()
