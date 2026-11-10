// Logging breakpoints in game code: `--break=eboot.bin+0x9f878` prints the
// registers (and the memory rdi/rax point at) each time that instruction runs,
// then the game continues. Works by swapping in int3 and single-stepping over it.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/module.h"

namespace core::debugger {

// "module+0xoffset" specs; resolved and armed once modules are loaded.
void Arm(const std::vector<std::string>& specs, const std::vector<Module*>& modules);

// LBP3_HWWATCH_RING=1: writes the recorded watchpoint hits to analysis\hwwatch_ring.txt (F10).
void DumpWatchRing();

}  // namespace core::debugger
