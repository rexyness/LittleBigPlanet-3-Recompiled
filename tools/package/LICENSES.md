# Licences

- **This port and its runtime:** GNU GPL, version 3 or (at your option) any later version
  (`LICENSE-LBP3-Recompiled.txt`). Up to v0.1.2 it was GPL-2.0-or-later; PurpleGitty's original runtime,
  which it builds on, is GPL-2.0-or-later and is used under version 3.
- **shadPS4** (the graphics core): GPL-2.0-or-later (`third_party\shadPS4\`, with the texts of the other licences some of
  its files use).
- **Code from LBPPC** (emurphy04): GPL-3.0-or-later for its runtime, GPL-2.0-or-later for its shadPS4 files, as
  marked in the source.
- **Libraries linked into the programs:** each library's own licence is in `third_party\<name>\`. They're
  permissive (MIT, BSD, zlib, Boost, ISC/OpenSSL for LibreSSL), and some are Apache-2.0 (abseil, the Vulkan
  headers). Apache-2.0 code can be combined with GPL version 3 but not with version 2 alone, so these
  programs as built are distributed under the terms of the **GNU GPL version 3** (`GPL-3.0.txt`),
  which every GPL part above allows ("or any later version").
- **stb** (stb_image and friends): public domain or MIT, at your choice (`third_party\stb\LICENSE.txt`, from
  the header).
- **The Microsoft Visual C++ runtime DLLs** (msvcp140*.dll, vcruntime140*.dll) are redistributed under the
  Visual Studio redistributable terms.

The complete corresponding source is described in `SOURCE.md`.

This is a summary for convenience, not legal advice.
