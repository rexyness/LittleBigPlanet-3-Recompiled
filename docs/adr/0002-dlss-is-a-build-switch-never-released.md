# DLSS is a build switch, never in a release

The port is GPL-2.0-or-later and built on other people's GPL code (the base and shadPS4), so it can't add a licence exception of its own. NVIDIA's DLSS SDK licence says, in §4(e): "You may not use the SDK in any manner that would cause it to become subject to an open source software license. As examples, licenses that require as a condition of use, modification, and/or distribution that the SDK be: (i) disclosed or distributed in source code form; (ii) licensed for the purpose of making derivative works; or (iii) redistributable at no charge" ([LICENSE.txt](https://github.com/NVIDIA/DLSS/blob/main/LICENSE.txt), checked 2026-11-21). NVIDIA's own GPL-2.0 Quake II RTX turned DLSS down for that reason: "we can't integrate DLSS 2.0 here because of the GPL restrictions" ([NVIDIA/Q2RTX#68](https://github.com/NVIDIA/Q2RTX/issues/68), checked 2026-11-21).

So DLSS sits behind a build switch that is off by default. NVIDIA's SDK (its headers, its static NGX library and `nvngx_dlss.dll`) never enters the repository; whoever builds with the switch on downloads it themselves (pinned: v310.9.16, 2026-10-16) into a folder outside every repository. A DLSS build is for the person who made it and is never published, and the release packaging refuses to package one. Our reading, not legal advice: the GPL's obligations attach when the program is distributed, so a build nobody hands out stays outside them. The port's own code that calls DLSS is ours and lives in the repository like any other code.

## Considered Options

- **Ship DLSS in the release zips.** Rejected: §4(e) and NVIDIA's answer on Q2RTX.
- **Call the NVIDIA driver's `_nvngx.dll` directly, without NVIDIA's static library** (as OptiScaler, GPL-3.0, does). Rejected: NVIDIA doesn't document that interface (its programming guide says to link the static NGX library), and whether it avoids the conflict is legally unsettled.
- **Ship without NVIDIA's DLL and have players download it** (as PrBoom-plus-RT, GPL-2.0, does). Rejected: the port would still be linked against the SDK, and whether that cures the conflict is unsettled.
- **An upscaler that is open source, such as AMD's FSR (MIT), for public players.** Deferred to its own follow-up. Most of the DLSS work is vendor-neutral (jitter, 16-bit motion vectors, the render-size and output-size model, putting the screen HUD back, frame generation's order), so a second upscaler reuses it.

## Consequences

Public players get no upscaler from this work, and the launcher shows no Upscaler setting in a public build. NVIDIA's rules for its logos on splash, about and credits screens apply only to distributed programs, so they don't apply here.
