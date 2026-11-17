import json, os, sys, urllib.request, urllib.error

TOKEN = (os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN")
         or open(os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), ".gh_token"),
                 encoding="utf-8-sig").read().strip())
OWNER, REPO = "headoffundefined58-arch", "-SnowShovel-Nivalis"
RUN_ID = sys.argv[1] if len(sys.argv) > 1 else None

OP = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def api(path, allow=(200,)):
    req = urllib.request.Request(
        "https://api.github.com" + path,
        headers={"Authorization": "Bearer " + TOKEN,
                 "Accept": "application/vnd.github+json",
                 "X-GitHub-Api-Version": "2022-11-28",
                 "User-Agent": "logdump"},
    )
    try:
        with OP.open(req, timeout=60) as r:
            return json.loads(r.read() or b"{}")
    except urllib.error.HTTPError as e:
        body = e.read()[:300].decode("utf-8", "replace")
        print(f"[api] {path} -> {e.code} {body}", file=sys.stderr)
        return None


class NoRedir(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *a, **k):
        return None


if not RUN_ID:
    runs = api(f"/repos/{OWNER}/{REPO}/actions/runs?per_page=1") or {}
    RUN_ID = runs["workflow_runs"][0]["id"]
    print("run:", RUN_ID, runs["workflow_runs"][0]["head_sha"][:8])

jobs = api(f"/repos/{OWNER}/{REPO}/actions/runs/{RUN_ID}/jobs") or {}
for j in jobs.get("jobs", []):
    if j.get("conclusion") == "success":
        continue
    print(f"\n{'='*70}\nJOB {j['name']} ({j['conclusion']})\n{'='*70}")
    for st in j.get("steps", []):
        if st.get("conclusion") in ("failure", "cancelled"):
            print(f"  FAILED STEP: {st['name']}")
    req = urllib.request.Request(
        f"https://api.github.com/repos/{OWNER}/{REPO}/actions/jobs/{j['id']}/logs",
        headers={"Authorization": "Bearer " + TOKEN, "User-Agent": "logdump"})
    op = urllib.request.build_opener(NoRedir, urllib.request.ProxyHandler({}))
    try:
        op.open(req, timeout=60)
        print("  (未预期：直接返回)")
    except urllib.error.HTTPError as e:
        loc = e.headers.get("Location")
        if not loc:
            print("  无法获取日志", e.code)
            continue
        r2 = urllib.request.Request(loc, headers={"User-Agent": "logdump"})
        with OP.open(r2, timeout=120) as resp:
            text = resp.read().decode("utf-8", "replace")
    lines = text.splitlines()
    # 优先显示错误上下文
    keys = ("error", "Error", "error:", "fatal", "CMake Error", "MSB", "Traceback",
            "无法", "找不到", "失败")
    hit = [i for i, l in enumerate(lines) if any(k in l for k in keys)]
    if hit:
        shown = set()
        for i in hit[:12]:
            for k in range(max(0, i - 4), min(len(lines), i + 6)):
                if k not in shown:
                    shown.add(k)
                    print(f"  {lines[k]}")
            print("  ---")
    print("\n  ---- 日志末尾 40 行 ----")
    for l in lines[-40:]:
        print("  " + l)
