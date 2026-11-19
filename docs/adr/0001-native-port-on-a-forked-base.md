# Native port forked from LBP3 Recompiled

LittleBigPlanet 3 on PS4 is already x86-64 code, so the port runs the original eboot directly on the PC's processor. A runtime stands in for the PS4 system libraries, and a shadPS4-derived video core handles graphics. The runtime is not written from scratch: it is a local fork of [PurpleGitty/LittleBigPlanet-3-Recompiled](https://github.com/PurpleGitty/LittleBigPlanet-3-Recompiled), a GPL-2.0-or-later native Windows port that targets this same dump (US CUSA00473, 1.28). As of 2026-10-28 it reaches the Pod, the Prologue levels and Create mode. The fork inherits its runtime, its shadPS4 fork (branch `lbp3`), its toolchain (Visual Studio 2026 with clang-cl, CMake, Ninja) and its license.

## Considered Options

- **Static recompilation or decompilation into C/C++.** Rejected: there's no foreign instruction set to translate.
- **Running LBP3 in the shadPS4 emulator.** Rejected as the goal, because the aim is a standalone port. The shadPS4 compatibility tracker also has no LBP3 report past menus (2026-11).
- **Forking a port from the bloodborne_pc → gow3_pc family** (gravityrush_pc, iss_pc, issn_pc). This was the first choice, before LBP3 Recompiled turned up. Rejected: none of that family's public LBP3 attempts gets past the "Connecting" screen (TheNIKITO/LittleBigPlanet-3---PC is stuck there). The one that gets further, btsouth/lbp3-pc, targets Linux and EU 1.00. Starting from a family port would mean redoing the network-join, fiber and GPU fixes LBP3 Recompiled already made.
- **Contributing upstream only, with no local fork.** Deferred: upstream is one author's single public drop (2 commits, no issues or PRs). Work happens in a local clone that tracks it as `upstream`. Whether to publish a GitHub fork or send pull requests is the user's call, made later.
