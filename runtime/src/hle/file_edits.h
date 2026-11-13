// Virtual edits to game files (best-of-both-worlds branch, Quit Game in the pause menu).
//
// The game files are never changed. A small description (tools/quit_menu.py writes it,
// user\file_edits.bin) says, per file: the file's real size, where its view is cut, the bytes
// appended after the cut, and byte patches inside the kept part. The file layer
// (hle/kernel_fs.cpp) shows edited files through that view: open, read, pread (and so mmap),
// lseek, fstat and stat all see the edited size and bytes.
//
// The format ("LBPEDIT1", version 2, little-endian) is the one LBPPC's scripts/quit_menu.py
// writes (LBPPC by emurphy04, aad6a91, GPL-2.0-or-later); this reader is our own:
//   "LBPEDIT1" u32 version
//   per file until the end: u16 nameLen, name (relative to /app0, '/'-separated),
//     u64 original size, u64 cut, u64 addLen, add bytes,
//     u32 patchCount, then per patch: u64 offset, u32 len, bytes
// An edit applies only while the file's real size equals `original` (another game version, or a
// different dump, leaves the file alone).
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace hle::file_edits {

struct Patch {
    uint64_t offset = 0;
    std::vector<uint8_t> bytes;
};

struct Edit {
    std::string guest;  // "/app0/patch_001.farc"
    uint64_t original = 0, cut = 0;
    std::vector<uint8_t> add;
    std::vector<Patch> patches;
    uint64_t Size() const { return cut + add.size(); }
};

// Parses a description; false (and `error` set) if it's malformed. Edits whose cut is past the
// original size, or whose patches leave the kept part, are rejected.
bool Parse(const std::vector<uint8_t>& data, std::vector<Edit>& out, std::string& error);

// LBP3_FILE_EDITS=<path> (the launcher sets it): loads the description once. Without it, or if
// it can't be read, no file is edited.
void Load();
// The edit for a guest path ("/app0/..."), if the file's real size matches; else nullptr.
std::shared_ptr<const Edit> Find(std::string_view guest, uint64_t hostSize);

// Reads [offset, offset+len) of the edited view into `out`. `readHost(buf, at, n)` reads the
// real file (returns bytes read, or < 0 on error). Returns the bytes produced (short at the
// view's end), or the host error.
template <typename ReadHost>
int64_t Read(const Edit& e, uint8_t* out, uint64_t offset, uint64_t len, ReadHost&& readHost) {
    const uint64_t size = e.Size();
    if (offset >= size || len == 0) return 0;
    if (len > size - offset) len = size - offset;
    uint64_t done = 0;
    if (offset < e.cut) {  // the kept part of the real file
        const uint64_t n = (len < e.cut - offset) ? len : e.cut - offset;
        const int64_t got = readHost(out, offset, n);
        if (got < 0) return got;
        if (static_cast<uint64_t>(got) < n) return got;  // the real file ended early
        for (const Patch& p : e.patches) {  // patches overlapping [offset, offset+n)
            const uint64_t a = p.offset > offset ? p.offset : offset;
            const uint64_t pEnd = p.offset + p.bytes.size(), rEnd = offset + n;
            const uint64_t b = pEnd < rEnd ? pEnd : rEnd;
            for (uint64_t i = a; i < b; ++i) out[i - offset] = p.bytes[i - p.offset];
        }
        done = n;
    }
    if (done < len) {  // the appended bytes
        const uint64_t from = offset + done - e.cut;
        const uint64_t n = len - done;
        for (uint64_t i = 0; i < n; ++i) out[done + i] = e.add[from + i];
        done += n;
    }
    return static_cast<int64_t>(done);
}

}  // namespace hle::file_edits
