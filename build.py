"""Configure / build Nivalis for one runtime.

vcvars64.bat is blocked on this machine (reg.exe policy). With the VS
generator MSBuild locates the toolchain itself, so no env capture is needed.
MSBuild's env Hashtable is case-insensitive: duplicate names such as
HTTP_PROXY / http_proxy cause MSB6001, so dedupe and drop proxy vars.

Usage:  python build.py            # AE, build2/   (default)
        python build.py --se       # SE, build-se/
        python build.py --se --configure
"""
import os
import shutil
import subprocess
import sys

SOURCE = r"D:\dev\SnowShovel"
CMAKE_CANDIDATES = [
    r"D:\dev\tools\cmake\cmake-4.2.0-windows-x86_64\bin\cmake.exe",
    r"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE"
    r"\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
]


def find_cmake() -> str:
    for path in CMAKE_CANDIDATES:
        if os.path.isfile(path):
            return path
    return shutil.which("cmake") or "cmake"


def clean_env() -> dict:
    env = {}
    seen = set()
    for key, value in os.environ.items():
        low = key.lower()
        if low in seen or low.endswith("_proxy"):
            continue
        seen.add(low)
        env[key] = value
    return env


def run(cmd: list, env: dict) -> int:
    print(f"\n$ {' '.join(cmd)}\n", flush=True)
    proc = subprocess.run(
        cmd, env=env, capture_output=True, text=True,
        encoding="utf-8", errors="replace",
    )
    tail = 8000
    if proc.stdout:
        print(proc.stdout[-tail:], flush=True)
    if proc.stderr:
        print(proc.stderr[-tail:], flush=True)
    print(f"[exit {proc.returncode}]", flush=True)
    return proc.returncode


def main() -> int:
    runtime = "SE" if "--se" in sys.argv else "AE"
    build = os.path.join(SOURCE, "build-se" if runtime == "SE" else "build2")
    env = clean_env()
    cmake = find_cmake()

    print(f"target runtime: {runtime}  build dir: {build}\ncmake: {cmake}", flush=True)
    # 不写 -G：让 CMake 选本机最新的 VS 生成器。写死 "Visual Studio 17 2022"
    # 在只有 VS18 的机器（含 CI 的 windows-2025）上会直接报找不到实例。
    # -A x64 保留。已有 build2 缓存会沿用其原生成器，不受影响。
    rc = run([cmake, "-B", build, "-S", SOURCE,
              "-A", "x64",
              f"-DNIVALIS_TARGET_RUNTIME={runtime}"], env)
    if rc != 0 or "--configure" in sys.argv:
        return rc

    return run([cmake, "--build", build, "--config", "Release"], env)


if __name__ == "__main__":
    sys.exit(main())
