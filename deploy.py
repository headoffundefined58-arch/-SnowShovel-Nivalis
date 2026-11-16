"""Copy the built dll (and its .pdb) into its MO2 mod folder / release dist.

Default = AE build (build2) -> MO2 mod folder 【自制】雪相关玩法, unchanged.
--se    = SE build (build-se) -> dist/SE/, ready to zip for SE 1.5.97 users.

The .pdb ships next to the dll because dbghelp can only symbolise a minidump
when the CodeView GUID inside the loaded dll finds a matching .pdb on disk.
"""
import os
import shutil
import sys

ROOT = r"D:\dev\SnowShovel"
AE_BUILD = os.path.join(ROOT, "build2", "Release")
SE_BUILD = os.path.join(ROOT, "build-se", "Release")

AE_TARGET = r"E:\MO2（2.5.2）\AE-mods\【自制】雪相关玩法\SKSE\Plugins"
SE_TARGET = os.path.join(ROOT, "dist", "SE", "SKSE", "Plugins")
BACKUP_DIR = r"E:\MO2（2.5.2）\mod-tools\backup"


def main() -> int:
    se = "--se" in sys.argv
    src = SE_BUILD if se else AE_BUILD
    dst = SE_TARGET if se else AE_TARGET
    source = os.path.join(src, "Nivalis.dll")
    source_pdb = os.path.join(src, "Nivalis.pdb")

    if not os.path.isfile(source):
        print(f"not built yet: {source}")
        return 1

    os.makedirs(dst, exist_ok=True)
    target = os.path.join(dst, "Nivalis.dll")

    if os.path.isfile(target) and not se:
        os.makedirs(BACKUP_DIR, exist_ok=True)
        backup = os.path.join(BACKUP_DIR, "Nivalis.dll.bak")
        shutil.copy2(target, backup)
        print(f"backup  -> {backup}")

    shutil.copy2(source, target)
    print(f"deploy  -> {target} ({os.path.getsize(target)} bytes)")

    if os.path.isfile(source_pdb):
        target_pdb = os.path.join(dst, "Nivalis.pdb")
        shutil.copy2(source_pdb, target_pdb)
        print(f"symbols -> {target_pdb} ({os.path.getsize(target_pdb)} bytes)")
    else:
        print(f"WARNING : no .pdb next to the build output ({source_pdb})")

    if se:
        # Ship a complete SE package: ini + Nivalis/ config folder.
        mod = r"E:\MO2（2.5.2）\AE-mods\【自制】雪相关玩法\SKSE\Plugins"
        ini = os.path.join(mod, "Nivalis.ini")
        if os.path.isfile(ini):
            shutil.copy2(ini, os.path.join(dst, "Nivalis.ini"))
            print("config  -> Nivalis.ini")
        cfg = os.path.join(mod, "Nivalis")
        if os.path.isdir(cfg):
            shutil.copytree(cfg, os.path.join(dst, "Nivalis"),
                            dirs_exist_ok=True)
            print("config  -> Nivalis/")
    return 0


if __name__ == "__main__":
    sys.exit(main())
