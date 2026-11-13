// The driver pipeline cache's file (shadPS4 video_core/renderer_vulkan/vk_driver_cache_file.h,
// fix/pipeline-warmup): a saved cache is only handed to the driver when it was made on this GPU,
// by this driver version, with this pipelineCacheUUID, and is whole. Offline; no GPU.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "video_core/renderer_vulkan/vk_driver_cache_file.h"

namespace {
int g_checks = 0, g_failed = 0;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        ++g_checks;                                                                                \
        if (!(c)) {                                                                                \
            ++g_failed;                                                                            \
            std::printf("  FAIL (line %d): %s\n", __LINE__, #c);                                   \
        }                                                                                          \
    } while (0)

using namespace Vulkan::DriverCacheFile;

Identity MakeId() {
    Identity id{};
    id.vendor_id = 0x10de;    // NVIDIA
    id.device_id = 0x2204;    // RTX 3090
    id.driver_version = 0x9c2a8000;
    for (std::size_t i = 0; i < kUuidSize; ++i) id.uuid[i] = static_cast<std::uint8_t>(0xa0 + i);
    return id;
}

// What vkGetPipelineCacheData returns: the driver's 32-byte header, then its blob.
std::vector<std::uint8_t> DriverData(const Identity& id, std::size_t body = 4096) {
    VkHeaderOne vh{};
    vh.header_size = sizeof(VkHeaderOne);
    vh.header_version = kVkHeaderVersionOne;
    vh.vendor_id = id.vendor_id;
    vh.device_id = id.device_id;
    std::memcpy(vh.uuid, id.uuid.data(), kUuidSize);
    std::vector<std::uint8_t> d(sizeof(vh) + body);
    std::memcpy(d.data(), &vh, sizeof(vh));
    for (std::size_t i = 0; i < body; ++i) d[sizeof(vh) + i] = static_cast<std::uint8_t>(i * 7);
    return d;
}

bool Rejected(const Identity& id, const std::vector<std::uint8_t>& file) {
    std::string why;
    const auto out = Unwrap(id, file, &why);
    return out.empty() && !why.empty();
}
}  // namespace

int main() {
    const Identity id = MakeId();
    const auto data = DriverData(id);
    const auto file = Wrap(id, data);

    // Round trip: the driver gets back exactly what it gave.
    {
        std::string why = "x";
        const auto out = Unwrap(id, file, &why);
        CHECK(out == data);
        CHECK(why.empty());
        CHECK(file.size() == sizeof(FileHeader) + data.size());
    }
    // No file: nothing to load, and nothing to complain about.
    {
        std::string why = "x";
        CHECK(Unwrap(id, {}, &why).empty());
        CHECK(why.empty());
    }
    // Cut short, anywhere.
    for (std::size_t cut : {std::size_t{1}, std::size_t{20}, sizeof(FileHeader) - 1,
                            sizeof(FileHeader), sizeof(FileHeader) + 10, file.size() - 1}) {
        CHECK(Rejected(id, std::vector<std::uint8_t>(file.begin(), file.begin() + cut)));
    }
    // Padded.
    {
        auto f = file;
        f.push_back(0);
        CHECK(Rejected(id, f));
    }
    // Not ours: a raw driver blob (an old or foreign file), or garbage.
    CHECK(Rejected(id, data));
    CHECK(Rejected(id, std::vector<std::uint8_t>(256, 0xcc)));
    // Another format version.
    {
        auto f = file;
        f[4] ^= 1;
        CHECK(Rejected(id, f));
    }
    // Another GPU, driver version or UUID.
    {
        Identity other = id;
        other.vendor_id = 0x1002;
        CHECK(Rejected(other, file));
        other = id;
        other.device_id ^= 1;
        CHECK(Rejected(other, file));
        other = id;
        other.driver_version += 1;
        CHECK(Rejected(other, file));
        other = id;
        other.uuid[15] ^= 0xff;
        CHECK(Rejected(other, file));
    }
    // Our header right, the driver's own header wrong (a corrupt body).
    {
        Identity foreign = id;
        foreign.uuid[0] ^= 1;
        CHECK(Rejected(id, Wrap(id, DriverData(foreign))));  // the driver header's UUID
        auto d = DriverData(id);
        d[4] = 2;  // header version
        CHECK(Rejected(id, Wrap(id, d)));
        d = DriverData(id);
        d[0] = 8;  // header size smaller than the header
        CHECK(Rejected(id, Wrap(id, d)));
        d = DriverData(id, 0);
        d[0] = 255;  // header size beyond the data
        CHECK(Rejected(id, Wrap(id, d)));
        CHECK(Rejected(id, Wrap(id, std::vector<std::uint8_t>(16, 1))));  // shorter than the header
    }
    // A wrapped empty cache isn't loadable either (the driver header is missing).
    CHECK(Rejected(id, Wrap(id, {})));

    std::printf("vkcachetest: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
