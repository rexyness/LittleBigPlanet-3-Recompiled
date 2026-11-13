// SPDX-License-Identifier: GPL-2.0-or-later
// "Quit Game" in LBP3's own pause menu, in place of Good Grief!: writes user\file_edits.bin for the
// runtime's file layer (LBP3_FILE_EDITS; the runtime answers the two keys in hle/quit.cpp).
// A C++ port of LBPPC's scripts/quit_menu.py (https://github.com/emurphy04/LBPPC by emurphy04, at
// aad6a91, GPL-2.0-or-later; our copy is tools/quit_menu.py), so the launcher needs no Python.
// The game files are never changed: file_edits.bin tells the runtime to show patch_001.farc with
// the edited pause-menu script added after its data, and the GUID map with that entry's size and
// hash changed.
#pragma once

#include <filesystem>
#include <string>

namespace launcher {

struct QuitMenuResult {
    bool ok = false;
    std::string message;  // one line for the log ("Quit Game: added ..." / "not added (why)")
};
// folder: the one holding patch_001.farc and output\orbisguids.map (the patch folder when the dump
// keeps base and patch apart, else the merged CUSA00473). out: the file_edits.bin to write (any
// old one is removed first, so a failed run leaves none).
QuitMenuResult WriteQuitMenuEdits(const std::filesystem::path& folder, const std::filesystem::path& out);
// <game_dir>\CUSA00473-patch or <game_dir>\CUSA00473, whichever has both files; empty if neither.
std::filesystem::path QuitMenuFolder(const std::filesystem::path& game_dir);

}  // namespace launcher
