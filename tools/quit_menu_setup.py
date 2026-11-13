"""Prepares Quit Game in the pause menu for lbp3.exe (best/README.md): finds the user's game
folder the way lbp3.exe does (tools/gamedir.py), and has tools/quit_menu.py (LBPPC's,
GPL-2.0-or-later) write user\\file_edits.bin from it. Prints one line; exit code 0 when the edit
was written (the launcher then sets LBP3_FILE_EDITS and LBP3_QUIT_MENU), 1 otherwise, in which
case the game starts with the menu as it is.

The pause menu script and the GUID map live in the patch's files: the folder holding them is
the patch folder (CUSA00473-patch) when the dump keeps base and patch apart, else the merged
CUSA00473.
"""
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import gamedir  # noqa: E402
import quit_menu  # noqa: E402


def main():
    game = gamedir.game_dir()
    folders = [game / "CUSA00473-patch", game / "CUSA00473"]
    folder = next((f for f in folders if (f / quit_menu.FARC).exists() and (f / quit_menu.MAP).exists()), None)
    if folder is None:
        print(f"Quit Game: not added (no {quit_menu.FARC} with {quit_menu.MAP} under {game})")
        return 1
    out = ROOT / "user"
    target = out / "file_edits.bin"
    try:
        quit_menu.prepare(folder, out)
    except Exception as error:  # noqa: BLE001  (the game starts without the button rather than not at all)
        target.unlink(missing_ok=True)
        print(f"Quit Game: not added ({type(error).__name__}: {error})")
        return 1
    return 0 if target.exists() else 1


if __name__ == "__main__":
    sys.exit(main())
