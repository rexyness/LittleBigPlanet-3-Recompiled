// XXH3_64bits of byte records, for res_cache_merge.py (the names of shadPS4's graphics pipeline
// key files are XXH3_64 of the key struct). Input: a file of records [u32 little-endian length]
// [bytes]; output: one 0x%016llx line per record. Build (from a VS x64 prompt):
//   clang-cl /O2 /I <shadPS4>\externals\xxhash xxh3tool.cpp /Fe:xxh3tool.exe
#define XXH_INLINE_ALL
#include <xxhash.h>

#include <cstdint>
#include <cstdio>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: xxh3tool <records file>\n");
        return 2;
    }
    FILE* f = std::fopen(argv[1], "rb");
    if (!f) {
        std::fprintf(stderr, "cannot open %s\n", argv[1]);
        return 2;
    }
    std::vector<unsigned char> buf;
    for (;;) {
        uint32_t len = 0;
        if (std::fread(&len, 4, 1, f) != 1) break;
        buf.resize(len);
        if (len && std::fread(buf.data(), 1, len, f) != len) {
            std::fprintf(stderr, "truncated record\n");
            return 1;
        }
        std::printf("0x%016llx\n", static_cast<unsigned long long>(XXH3_64bits(buf.data(), len)));
    }
    std::fclose(f);
    return 0;
}
