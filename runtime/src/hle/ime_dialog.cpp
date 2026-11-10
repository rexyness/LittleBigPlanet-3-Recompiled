// libSceImeDialog (text entry) and libSceIme (keyboard). Milestone 2, gap 12.
// Until implemented, these functions fall through to the linker's stubs.
#include "hle/register.h"

namespace hle {

void RegisterImeDialog(core::Linker& linker) { (void)linker; }

}  // namespace hle
