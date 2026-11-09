"""check_exe_dlls.py -- would any built program stop with a "missing DLL" popup? (docs/project/02_VISUAL_STUDIO.md §7)

Reads the import table of every .exe in the given folders (default: each Build/<preset>/bin and the AIGamestyle
apps' build folders), and follows each DLL it needs the way Windows does: the exe's own folder, then System32, then
the Windows folder. PATH is NOT used, because a double-click or F5 may not have the same PATH as your terminal.
Every DLL found next to the exe is checked the same way, recursively.

    python tools/check_exe_dlls.py                  every known build folder
    python tools/check_exe_dlls.py Build/windows-debug/bin  one folder (or .exe files)

Exit 0: nothing missing. Exit 1: the missing DLLs are listed, per program.
Qt plugins (platforms/qwindows.dll) are checked too when the exe needs Qt: no platforms plugin is also a popup.
"""
import os
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WINDIR = Path(os.environ.get("WINDIR", r"C:\Windows"))
SYSTEM_DIRS = [WINDIR / "System32", WINDIR]
# Supplied by Windows itself through API sets; never files you ship.
API_SET_PREFIXES = ("api-ms-win-", "ext-ms-")
# The Visual C++ runtime: in System32 once the VC++ redistributable is installed (Visual Studio installs it).
VC_RUNTIME = {"vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll", "msvcp140_1.dll", "msvcp140_2.dll",
              "concrt140.dll", "vccorlib140.dll", "vcruntime140d.dll", "vcruntime140_1d.dll", "msvcp140d.dll",
              "ucrtbased.dll"}


def pe_imports(path: Path) -> list[str]:
    """The DLL names in a PE file's import table (normal and delay-load imports are not distinguished: delay-loaded
    DLLs are listed only by the delay table, which is skipped, because a missing one does not stop the program)."""
    data = path.read_bytes()
    if data[:2] != b"MZ":
        return []
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        return []
    n_sections = struct.unpack_from("<H", data, pe + 6)[0]
    opt_size = struct.unpack_from("<H", data, pe + 20)[0]
    opt = pe + 24
    magic = struct.unpack_from("<H", data, opt)[0]
    dir_base = opt + (112 if magic == 0x20B else 96)
    import_rva = struct.unpack_from("<I", data, dir_base + 8)[0]
    if import_rva == 0:
        return []
    sections = []
    sec = opt + opt_size
    for i in range(n_sections):
        vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", data, sec + i * 40 + 8)
        sections.append((vaddr, max(vsize, rsize), raddr))

    def off(rva):
        for vaddr, size, raddr in sections:
            if vaddr <= rva < vaddr + size:
                return rva - vaddr + raddr
        return None

    names = []
    o = off(import_rva)
    while o is not None and o + 20 <= len(data):
        name_rva = struct.unpack_from("<I", data, o + 12)[0]
        if name_rva == 0:
            break
        no = off(name_rva)
        if no is not None:
            end = data.index(b"\0", no)
            names.append(data[no:end].decode("ascii", "replace"))
        o += 20
    return names


def find_dll(name: str, exe_dir: Path) -> Path | None:
    for d in [exe_dir, *SYSTEM_DIRS]:
        p = d / name
        if p.is_file():
            return p
    return None


def check_exe(exe: Path) -> tuple[list[str], list[str]]:
    """(missing DLLs with who needs them, notes)."""
    exe_dir = exe.parent
    missing, notes, seen = [], [], set()
    todo = [(exe, n) for n in pe_imports(exe)]
    uses_qt = False
    while todo:
        owner, name = todo.pop()
        key = name.lower()
        if key in seen or key.startswith(API_SET_PREFIXES):
            continue
        seen.add(key)
        if key.startswith("qt6"):
            uses_qt = True
        found = find_dll(name, exe_dir)
        if found is None:
            hint = " (the Visual C++ redistributable)" if key in VC_RUNTIME else ""
            missing.append(f"{name}{hint}, needed by {owner.name}")
            continue
        if found.parent == exe_dir:   # ours: follow what it needs too; Windows' own DLLs are trusted
            todo += [(found, n) for n in pe_imports(found)]
    if uses_qt:
        plat = exe_dir / "platforms"
        if not any(plat.glob("qwindows*.dll")):
            missing.append("platforms/qwindows.dll (Qt cannot start without its platform plugin: run windeployqt)")
    return missing, notes


def default_folders() -> list[Path]:
    out = sorted(p / "bin" for p in (ROOT / "Build").glob("windows-*") if (p / "bin").is_dir())
    for app in ("AIGamestyle/ComfyLive/build", "AIGamestyle/ComfyLive/build_vs", "AIGamestyle/App/build_vs",
                "AIGamestyle/App/out"):
        p = ROOT / app
        if p.is_dir():
            out += sorted({e.parent for e in p.rglob("*.exe") if "CMakeFiles" not in e.parts and "_deps" not in e.parts})
    return out


def main(argv: list[str]) -> int:
    targets = [Path(a) for a in argv] or default_folders()
    exes = []
    for t in targets:
        if t.is_file() and t.suffix.lower() == ".exe":
            exes.append(t)
        elif t.is_dir():
            exes += sorted(e for e in t.glob("*.exe"))
    if not exes:
        print("[check_exe_dlls] no programs found (build first)")
        return 1
    bad = 0
    for exe in exes:
        missing, _ = check_exe(exe)
        rel = exe.relative_to(ROOT) if exe.is_relative_to(ROOT) else exe
        if missing:
            bad += 1
            print(f"[MISSING] {rel}")
            for m in missing:
                print(f"          {m}")
        else:
            print(f"[ ok ]    {rel}")
    print(f"[check_exe_dlls] {len(exes) - bad} of {len(exes)} programs start without a missing-DLL popup")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
