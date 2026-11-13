"""Imports a decrypted PS4 LittleBigPlanet 3 save into this port's save folder.

    python tools\\ps4_save_import.py <folder with the decrypted PS4 save> [--dry-run]

The folder (any layout, searched recursively) must hold decrypted save files, as exported from a
PS4 by save tools that decrypt (encrypted "sdimg_" backups can't be read). They are recognised by
name:
    bigfart<N>                -> user\\savedata\\CUSA00473\\LBPxSAVE\\00000001\\
    littlefart<N>             -> user\\savedata\\CUSA00473\\LBPxSAVExLITTLE\\00000001\\
    playfart_<a>_<b>_<N>      -> user\\savedata\\CUSA00473\\LBPxSAVEx<a>x<b>\\00000001\\
When several of a kind are present, the one with the highest <N> (the newest) is taken. The
port's current save of that kind is moved out of the way first, so the game loads the imported
one. Before anything changes, all of user\\savedata\\CUSA00473 is copied to
analysis\\savedata_backups\\before_ps4_import_<date>_<time>. It refuses while the game runs or
while a test run has the saves set aside.

The file names and where they go are LBPPC's finding (emurphy04, scripts/save_import.py); this
script is our own.
"""
import argparse
import datetime
import pathlib
import re
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SAVES = ROOT / "user" / "savedata" / "CUSA00473"
BACKUPS = ROOT / "analysis" / "savedata_backups"
TEST_MARKER = ROOT / "analysis" / "savedata_live_is_test"

KINDS = [
    (re.compile(r"^bigfart(\d+)$"), lambda m: "LBPxSAVE"),
    (re.compile(r"^littlefart(\d+)$"), lambda m: "LBPxSAVExLITTLE"),
    (re.compile(r"^playfart_(\d+)_(\d+)_(\d+)$"), lambda m: f"LBPxSAVEx{m.group(1)}x{m.group(2)}"),
]
FART = re.compile(r"^(bigfart\d+|littlefart\d+|playfart_\d+_\d+_\d+)$")


def game_running():
    try:
        out = subprocess.run(["tasklist", "/fi", "imagename eq lbp3.exe"], capture_output=True, text=True).stdout
    except OSError:
        return False
    return "lbp3.exe" in out.lower()


def plan(source):
    """{target dir name: (number, path)} with the newest file of each kind."""
    chosen = {}
    for path in sorted(source.rglob("*")):
        if not path.is_file():
            continue
        for pattern, folder in KINDS:
            m = pattern.match(path.name)
            if not m:
                continue
            target = folder(m)
            number = int(m.groups()[-1])
            if target not in chosen or number > chosen[target][0]:
                chosen[target] = (number, path)
    return chosen


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("source", type=pathlib.Path)
    p.add_argument("--dry-run", action="store_true", help="show what would be imported, change nothing")
    a = p.parse_args()
    if not a.source.is_dir():
        print(f"No such folder: {a.source}")
        return 1
    chosen = plan(a.source)
    if not chosen:
        print("No decrypted LBP3 save files found (bigfart*, littlefart*, playfart_*). Encrypted PS4 backups can't be read.")
        return 1
    for target, (number, path) in sorted(chosen.items()):
        print(f"  {path.name:<28} -> {target}\\00000001\\")
    if a.dry_run:
        print("Dry run: nothing changed.")
        return 0
    if TEST_MARKER.exists():
        print("A test run has your saves set aside right now (analysis\\savedata_live_is_test). Try again after it.")
        return 1
    if game_running():
        print("Close the game first.")
        return 1
    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    backup = BACKUPS / f"before_ps4_import_{stamp}"
    if SAVES.exists():
        BACKUPS.mkdir(parents=True, exist_ok=True)
        shutil.copytree(SAVES, backup)
        print(f"Your current saves are backed up in {backup}")
    for target, (number, path) in sorted(chosen.items()):
        folder = SAVES / target / "00000001"
        folder.mkdir(parents=True, exist_ok=True)
        (SAVES / target / "sce_sys").mkdir(parents=True, exist_ok=True)
        for old in folder.iterdir():  # the port's own save of that kind (it's in the backup)
            if old.is_file() and FART.match(old.name):
                old.unlink()
        shutil.copy2(path, folder / path.name)
    print(f"Imported {len(chosen)} save file(s). Start the game to use them; the backup puts things back.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
