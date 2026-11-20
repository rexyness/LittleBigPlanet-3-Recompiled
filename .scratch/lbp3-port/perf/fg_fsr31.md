# FSR 3.1 frame generation for the port: study and spike (2026-11-12)

Lane B of the frame-generation prototype ([fps60.md](fps60.md), [fps60_render_re.md](fps60_render_re.md)). Part 1 is a read-only study of AMD's SDK, from its headers, docs and CMake files read on GitHub and gpuopen.com. Part 2 is the build spike the user approved ("ok", 2026-11-12). Labels: **header** (quoted from SDK source), **doc** (AMD docs), **inferred**.

## Part 1: what the SDK offers

### 1. Interpolating into our own textures, without AMD's swapchain: yes

Two levels can do it.

- **The FFX API (SDK 2.x, and the `ffx-api` folder in 1.1.x)** (header): `ffxDispatchDescFrameGeneration { commandList, presentColor, outputs[4], numGeneratedFrames, reset, backbufferTransferFunction, minMaxLuminance[2], generationRect, frameID }`, in `Kits/FidelityFX/framegeneration/include/ffx_framegeneration.h`, main @ v2.4.0 `7539ae8`.
  - The configure flag `FFX_FRAMEGENERATION_FLAG_NO_SWAPCHAIN_CONTEXT_NOTIFY` means "the context should only run frame interpolation and not modify the swapchain".
  - The provider (`framegeneration/fsr3/internal/ffx_provider_fsr3framegeneration.cpp`) skips every swapchain call when that flag is set. On Vulkan the swapchain ABI check is hard-coded valid.
  - Dispatch never touches the swapchain. It feeds `presentColor` to optical flow and interpolation (or the registered `HUDLessColor` for optical flow) and writes `outputs[0]`.
  - The docs call manual dispatch "possible, but *highly* discouraged … exists to support specific game-engine integrations such as the Unreal Engine plugin" (`docs/techniques/frame-interpolation-api.md`). That is exactly our case, since the port presents through SDL/D3D11, not a Vulkan swapchain.
  - **Catch** (doc, `docs/getting-started/ffx-api.md`): "An application using the AMD FSR™ API must use one of the provided signed DLLs": `amd_fidelityfx_loader_vk.dll` and `amd_fidelityfx_framegeneration_vk.dll` in `Kits/FidelityFX/signedbin`. These are binaries we can't build ourselves.
- **The component level (SDK 1.1.x `sdk/`, buildable from source)** (inferred from the 1.1.4 tree; checked in part 2): `ffxFrameInterpolationContextCreate`, `…Prepare` and `…Dispatch`, plus the optical-flow component, built as static libraries with the Vulkan backend `ffx_backend_vk`. The dispatch description has an output resource, so there is no swapchain. The FFX API provider above is a wrapper over exactly these calls.

Inputs per generated frame:
- the current colour (`presentColor`) and, optionally, the HUD-less colour;
- from prepare: depth, motion vectors, render size, motion-vector scale, jitter, frame time, camera near, far and FOV;
- internally, the previous interpolation source (kept by the context) and optical flow computed from the colour.

Output: one interpolated image at display size.

### 2. What prepare needs, and what the port can supply

Prepare V2 fields (header): `renderSize, jitterOffset, motionVectorScale, frameTimeDelta, reset, cameraNear, cameraFar, cameraFovAngleVertical, viewSpaceToMetersFactor, depth, motionVectors, cameraPosition/Up/Right/Forward[3]`. Create flags: `DEPTH_INVERTED`, `DEPTH_INFINITE`, `DISPLAY_RESOLUTION_MOTION_VECTORS`, `MOTION_VECTORS_JITTER_CANCELLATION`, `HIGH_DYNAMIC_RANGE`, `ASYNC_WORKLOAD_SUPPORT`.

| Input | The SDK's convention | The port | Gap |
|---|---|---|---|
| Motion vectors | 2× float; "encode the motion from a pixel in the current frame to the position of that same pixel in the previous frame"; `pixelMV = mv * motionVectorScale`, so the scale is the render size for UV-space vectors (doc `super-resolution-upscaler.md`; header comment). Internally 16-bit | 1080p RGBA8, decoded with uvescale (4,−4,0,0), R=1 marks geometry | **A conversion pass**: decode to RG16F in UV or pixel units, flip the sign if the game's vectors point forward, write 0 where R=0 (sky). Lane A's reprojection test settles the sign and scale |
| Depth | Any single float depth; the stencil of D32S8 is ignored. Inverted and infinite are flags | D32S8 at 1080p, convention unknown (forward or reversed) | Set the flags from lane A's dump (sky value 0 or 1). It can be passed directly as a sampled image, no conversion |
| Near, far | Used for depth separation and disocclusion (inferred). The debug checker warns that "cameraFar value is very low which may result in depth separation artifacting" | Unknown | Estimate from the game's constant buffers, or use plausible constants (e.g. 0.1 / 10000). Degradation is undocumented; tune by eye |
| Vertical FOV | Used by the internal depth reconstruction (inferred) | Unknown | The same: estimate or use a constant (LBP's camera is around 40–60°) |
| `viewSpaceToMetersFactor` | Scale only | Unknown | 1.0 |
| Camera position and orientation | V2 marks them "required" (doc), but the provider uses them only for a debug zero-vector warning; they never reach the interpolation description (header, provider source) | Unknown | Any non-zero vectors |
| Jitter | None with SMAA 1x | 0,0 | none |
| Frame time | Milliseconds | 33.3 | none |
| Reset | Disjoint `frameID` or `reset=true` | Scene cuts, loads, 0- and 2-step flips | The port sets `reset` from the flags the present lane drops frames on |

### 3. The UI

- **`HUDLessColor`** (header, doc): "used during frame interpolation to detect the UI and avoid distortion on UI elements". The HUD-less format can differ (`ffxCreateContextDescFrameGenerationHudless`). This fits the game: the screen HUD is drawn onto the display buffer after the final composite (fps60_render_re.md), so the display buffer copied before the HUD is the HUD-less input.
- **The other mode** is a present callback with a separate UI texture, composited on every frame. It doesn't apply here: the game draws its HUD into the same buffer.
- **The port's own ImGui overlays** (on-screen keyboard, dialogs) are drawn by the presenter after generation, on both frames, and never enter FSR.

### 4. Build fit

- **SDK 1.1.4 `sdk/`** (CMake, header):
  - `sdk/CMakeLists.txt` has the options `FFX_FSR3`, `FFX_FI` (frame interpolation) and `FFX_OF` (optical flow), `FFX_API_BACKEND VK_X64`, and `FFX_BUILD_AS_DLL` (off means static).
  - Shaders are compiled at build time by `FFX_SC_EXECUTABLE = sdk/tools/binary_store/FidelityFX_SC.exe`, which wraps `glslangValidator.exe` from the same folder, into permutation headers. The Vulkan frame-interpolation shaders use `-compiler=glslang … --target-env vulkan1.2 -DFFX_GLSL=1` (`backends/vk/CMakeShadersFrameinterpolation.txt`).
  - The VK backend does `find_package(Vulkan REQUIRED)` with a minimum Vulkan SDK of **1.3.250** (`backends/vk/CMakeLists.txt`) and links `Vulkan::Vulkan`.
  - The project is set up for the Visual Studio generators (2015–2022) and Ninja; nothing is said about clang-cl.
- **SDK 2.x** ships the signed DLLs; building them from source isn't offered for API use.
- **Loader fit** (local source, `third_party/shadPS4/src/video_core/renderer_vulkan/`):
  - shadPS4 uses vulkan-hpp with `VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1` (`vk_common.h:9`), loads `vulkan-1.dll` at run time (`vk_platform.cpp:275`), links only `Vulkan::Headers` (`CMakeLists.txt:1324`), and has `vkGetDeviceProcAddr` in `VULKAN_HPP_DEFAULT_DISPATCHER` (`vk_instance.cpp:623`).
  - FFX's `VkDeviceContext { vkDevice, vkPhysicalDevice, vkDeviceProcAddr }` (header `ffx_vk.h` in 1.1.4, `api/include/vk/ffx_api_vk.h` in 2.x) takes exactly that. Whether the 1.1.4 backend also calls instance-level functions directly (and so needs the `vulkan-1.lib` import) is checked in part 2.
  - The FFX API in 2.x accepts a custom queue-submit function (`VkQueueInfoFFXAPI.submitFunc`, for "concurrent submissions"), which matters because shadPS4 submits from its own threads.
- **Licence:** FSR 3 is MIT ("Available here on GPUOpen under the MIT license", gpuopen.com/fidelityfx-superresolution-3; `sdk/LICENSE.txt`). That is compatible with the port's GPL-2.0. In 2.x, `docs/license.md` puts files not on its permissive list under a binary-only, no-reverse-engineering AMD licence. The signed DLLs are on the permissive list, but that is one more reason to prefer building 1.1.4 from source.

### 5. Version to pin

- **Newest overall:** FSR SDK **v2.4.0** (`7539ae8`, 2026-10-28). It has FSR 3.1.x frame generation on Vulkan ("Vulkan® support at feature parity with DX12", "Driver-Assisted Frame Pacing on DirectX® 12 and Vulkan®"), but only through the signed DLLs.
- **Newest buildable from source:** FidelityFX SDK **v1.1.4** (`c6efa6b`, 2025-05-08): FSR 3.1.4, Vulkan backend, "General fixes to Vulkan Frame Interpolation Swapchain", Vulkan SDK ≥ 1.3.250.
- **Pinned for the spike: v1.1.4,** because the component-level path builds into the port's exe from source with no binary blobs at run time. v2.4.0 with the signed DLLs stays the fallback if 1.1.4 doesn't build.

### Quality notes

- **The base frame rate** (doc, gpuopen.com/fidelityfx-superresolution-3): "FSR 3 Frame Generation runs best when interpolating from a minimum of 60 fps … going below 60 is not recommended … interpolation visual artifacts being more prominent at lower frame rates. Sub 30fps frame rate pre-interpolation should be absolutely avoided." Our base is exactly 30, the worst supported case.
- **Motion vectors:** FSR uses 16-bit internally, so our 8-bit vectors (~0.94 × 0.53 px steps) are coarser than it expects. Optical flow from the colour images complements them.

## Part 2: build spike (stopped at the permission system)

The user approved downloading and building the SDK ("ok", in reply to "Want me to allow the AMD SDK download?").

- **Done: the clone.** `D:\Recomps\lbp3-wt\fsr\sdk114` is a shallow clone of tag **v1.1.4**, commit **`c6efa6bf7f2027b3ec94f28578bb5965eabb9e55`**, 556 MB, from the official repository only. It holds `sdk/` (sources and CMake), `sdk/tools/binary_store/` (`FidelityFX_SC.exe`, `glslangValidator.exe`, `dxcompiler.dll`, …), `ffx-api/` and `PrebuiltSignedDLL/` (`amd_fidelityfx_vk.dll/.lib`, the FFX API for Vulkan as a signed binary).
- **Blocked: the build.** Writing `D:\Recomps\lbp3-wt\fsr\build_ffx.bat` was denied by the Claude Code auto-mode classifier with the reason **"[Code from External]"**. Per the brief, I didn't work around it. That script would have done three things:
  1. Made an import library for `C:\Windows\System32\vulkan-1.dll` (`dumpbin /exports`, a `.def`, `lib /def`). This PC has no Vulkan SDK (`VULKAN_SDK` unset, no `C:\VulkanSDK`), and the FFX VK backend does `find_package(Vulkan REQUIRED)`, requires ≥ 1.3.250 and calls instance-level functions (`vkGetPhysicalDeviceProperties`, `vkGetPhysicalDeviceFeatures2`, `vkEnumerateDeviceExtensionProperties`, `vkGetPhysicalDeviceMemoryProperties`) directly. Device functions go through `vkDeviceProcAddr` (`src/backends/vk/ffx_vk.cpp:1451-1480`).
  2. Configured `sdk/` with CMake and Ninja, clang-cl from VS 18, `-DFFX_API_BACKEND=VK_X64 -DFFX_FSR3=ON -DFFX_FI=ON -DFFX_OF=ON`, the Vulkan headers from shadPS4's `externals/vulkan-headers` (`VK_HEADER_VERSION` 330, i.e. 1.4.330), `-DCMAKE_EXE_LINKER_FLAGS=/machine:x64` (the SDK's platform check, `sdk/CMakeLists.txt:83`), and static libraries.
  3. Built it. The build runs the downloaded `FidelityFX_SC.exe` and `glslangValidator.exe` from `binary_store` as a pre-build step to compile the shader permutations. That is the "code from external" execution.
- **Not started:** the offline harness, since it needs the libraries.
- **What would need the user's explicit permission:**
  - running a build that executes the SDK's bundled `FidelityFX_SC.exe` and `glslangValidator.exe`, through a script in `D:\Recomps\lbp3-wt\fsr\`;
  - or using `PrebuiltSignedDLL\amd_fidelityfx_vk.dll` at run time (also downloaded code).
  - The alternative that avoids the bundled binaries is to compile the GLSL shaders with the glslang already vendored in shadPS4 (`externals/glslang`, built from source in our own tree), replacing `FidelityFX_SC.exe` with a small CMake step that generates the same permutation headers. That is more work, and the permutation list would have to be reproduced from `CMakeCompileFrameinterpolationShaders.txt`.
  - An `autoMode.allow` rule for `D:\Recomps\lbp3-wt\fsr\**` builds (the user added a similar rule for the clone's `build.bat`) would cover the first option.

### What the component-level API needs (read from the clone)

- `FfxFrameInterpolationDispatchDescription` (`sdk/include/FidelityFX/host/ffx_frameinterpolation.h:247-282`) writes to `FfxResource output` and takes `currentBackBuffer` and `currentBackBuffer_HUDLess`. That confirms interpolation into our own image with no swapchain.
- At this level the caller also runs **optical flow itself** (`ffx_opticalflow.h`) and passes `opticalFlowVector`, `opticalFlowSceneChangeDetection`, the buffer size, scale and block size. The caller also owns the shared `dilatedDepth`, `dilatedMotionVectors` and `reconstructedPrevDepth` resources that prepare writes (`FfxFrameInterpolationPrepareDescription`, `:206-228`, with the same camera fields as the FFX API).
- The FFX API provider (`ffx-api`) is the reference for wiring these together: context creation, the shared resources, optical flow, then prepare, then dispatch. It is MIT source in the clone and can be copied in reduced form into the port.

## Verdict

**Go for FSR 3.1 as the quality upgrade, conditional on two things:** permission to build the SDK (or the glslang-vendored shader route), and lane A's dump confirming the vector sign and scale. It doesn't block the prototype: the port's own motion-vector warp ships first.

- **Feasible:** an output-to-our-texture dispatch exists at both levels; the inputs are all available or can be approximated (camera values don't reach the interpolation passes except near, far and FOV for depth handling); there is a native HUD-less input; it is MIT; and its device-function loading takes shadPS4's `vkGetDeviceProcAddr`.
- **Integration sketch:**
  - **Device and queue:** shadPS4's Vulkan device. Record on our own command buffer on the graphics queue at flip time, after the game's frame N is complete, so no async compute at first.
  - **Images, at the pre-HUD point of frame N:** copy the HUD-less colour. At the flip, take the final colour. Convert the game's RGBA8 velocity (sign, scale, sky→0) into an RG16F image; depth is used as is, with the inverted and infinite flags set from the dump.
  - **Dispatch sequence:** optical flow on the colour, then prepare (vectors and depth), then interpolation into our RGBA8 output. Read the output back with the real frame, and the presenter shows generated then real, with the ImGui overlays on both (lane C's present path).
  - **Reset:** set `reset` on cuts, loads and 0- or 2-step flips.
- **Risks:**
  - AMD calls a 30 fps base the worst supported case and manual dispatch "highly discouraged"; Unreal does the same.
  - The 8-bit vectors are coarser than the 16-bit FSR uses internally.
  - Near, far and FOV are approximated.
  - A 1080p GPU cost on the RTX 3090 is unmeasured; AMD quotes ~1 ms-class costs on recent GPUs.
  - Shader permutations are compiled for Vulkan 1.2 with possible 16-bit and wave features, which shadPS4's device must have enabled (unchecked).
  - Licence hygiene: copy only MIT files, keep the notice, and use no `signedbin` binaries.

Sources: [FidelityFX-SDK releases](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/releases), [tags](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/tags), [ffx_framegeneration.h @ main](https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/main/Kits/FidelityFX/framegeneration/include/ffx_framegeneration.h), [ffx_provider_fsr3framegeneration.cpp @ main](https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/main/Kits/FidelityFX/framegeneration/fsr3/internal/ffx_provider_fsr3framegeneration.cpp), [frame-interpolation-api.md](https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/main/Kits/FidelityFX/docs/techniques/frame-interpolation-api.md), [super-resolution-upscaler.md](https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/main/Kits/FidelityFX/docs/techniques/super-resolution-upscaler.md), [ffx-api.md](https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/main/Kits/FidelityFX/docs/getting-started/ffx-api.md), [license.md](https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/main/Kits/FidelityFX/docs/license.md), [ffx_api_vk.h](https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/main/Kits/FidelityFX/api/include/vk/ffx_api_vk.h), [v1.1.4 sdk/CMakeLists.txt](https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/v1.1.4/sdk/CMakeLists.txt), [v1.1.4 ffx-sc.md](https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/v1.1.4/docs/tools/ffx-sc.md), [gpuopen FSR 3](https://gpuopen.com/fidelityfx-superresolution-3/), [gpuopen frame interpolation 3.1.5](https://gpuopen.com/manuals/fidelityfx_sdk2/techniques/frame-interpolation/).
