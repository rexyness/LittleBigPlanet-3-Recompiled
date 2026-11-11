// Where libSceSaveData keeps each user's saves (hle/savedata.cpp), shared with the save-data
// dialog (hle/common_dialog.cpp).
#pragma once
#include <cstdint>
#include <filesystem>

namespace hle::savedata {

// user/savedata/<title id>/ for user 1 (unchanged from single player), and
// user/savedata/user<N>/<title id>/ for users 2-4, so a co-op guest's saves never mix with
// player 1's. A missing title id means CUSA00473.
std::filesystem::path SaveRoot(int32_t userId, const char* titleId);

}  // namespace hle::savedata
