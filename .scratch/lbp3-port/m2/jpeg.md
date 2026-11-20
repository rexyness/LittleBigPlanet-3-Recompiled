# Milestone 2, gap 13: libSceJpegDec and libSceJpegEnc

Branch `fix/jpeg` in `D:\Recomps\lbp3-wt\jpeg` (from `m2/scaffold` `baab2bf`), commits `0131179` (implementation and harness) and `f4016b3` (comment follow-up). Files: `runtime/src/hle/jpeg.h` (structures with `static_assert`ed offsets, entry points, error codes), `runtime/src/hle/jpeg.cpp` (implementation), `runtime/tests/jpegtest.cpp` (offline harness, target `jpegtest`).

## Summary

- **Encoder: reached offline, by photos of the scene taken from the Popit.** That is capture mode 1 of the Popit capture code at `0x702c80`. The game reads back the frame, encodes it with `sceJpegEncEncode` at half the render size, quality 70, and keeps the JPEG as the photo's image.
  - Capture Object (mode 2) and the capture near `poppet/camera_countdown` save DXT textures, not JPEGs.
  - The PlayStation Camera snapshot and the grief report also use the encoder, but they need a camera or the servers.
- **Decoder: not reached offline.** Its only caller loads image files that `libSceContentSearch` found (the PS4 capture gallery). Content search stays a stub and returns no results, so `sceJpegDecDecode` never runs in the port. The milestone's "photo or picture sticker" check therefore exercises the encoder only.
- **Built-in picture stickers and photos are decoded by the game itself.** The eboot links IJG libjpeg, and its texture loader decodes JPEG data with it. The shipped `*_sticker_NN.tex` files are plain JFIF files (640x360, baseline, 4:2:0). They have never touched `libSceJpegDec`.

## How call sites were found

`tools\disasm.py`'s `plt_names` maps each PLT thunk to its import. There is one call site per function:

| Import | NID | Thunk | Call |
|---|---|---|---|
| sceJpegEncQueryMemorySize | o6ZgXfFdWXQ | 0xe0fa40 | 0x4124b2 |
| sceJpegEncCreate | K+rocojkr-I | 0xe0fa50 | 0x4124e5 |
| sceJpegEncEncode | QbrU0cUghEM | 0xe0fa60 | 0x41254b |
| sceJpegEncDelete | j1LyMdaM+C0 | 0xe0fa70 | 0x412561 |
| sceJpegDecParseHeader | LSinoSQH790 | 0xe0fd70 | 0x90f266 |
| sceJpegDecQueryMemorySize | uNAUmANZMEw | 0xe0fd80 | 0x90f363 |
| sceJpegDecCreate | JPh3Zgg0Zwc | 0xe0fd90 | 0x90f3d0 |
| sceJpegDecDecode | 1kzQRoWEgSA | 0xe0fda0 | 0x90f4fc |
| sceJpegDecDelete | Hwh11+m5KoI | 0xe0fdb0 | 0x90f6c3 |

The encoder imports all sit in one function, `0x412450`. The decoder imports all sit in `0x90f100`. Earlier runs' logs (`analysis\*_stdout.txt`) contain no `[stub] libSceJpeg*` lines: no run so far has called either library. The `[stub] libSceContentSearch` lines appear about 150 times per run.

With the stubs, a photo would have been empty. The QueryMemorySize stub returns 0, and the game skips Create and Encode when the result is `<= 0`. It then keeps the w*h buffer with a length of 0 and hashes that as the photo's image.

## Encoder: who calls it

`0x412450(image, width, height, pitch, quality, int* outSize)` is wrapped by `0x4125a0(result, image, width, height, ByteArray* out, quality)`, which always passes pitch = 4 * width. That wrapper stores the JPEG as a resource keyed by its SHA-1 (`0x220de0`) and sets status 0x80000001 on success. Its callers:

1. **`0x401760`, the deferred image-save job.** It runs from the main loop (`0x401a50`, called at `0x5b3f`) four frames after the capture was queued. The job's format field (+0x40) chooses the output: 0 is a DXT texture through `0x411d10`, 1 is `0x4133c0`, and 2 is JPEG.
   - Format 2 encodes the main image at quality 70 (`0x401842`).
   - If the job has a thumbnail resource (+0x30), it encodes the half-size copy at quality 90 (`0x401965`), but only when +0x5a is set. Otherwise the copy is a DXT texture. This happens whatever the main image's format is.
     - +0x5a holds `0x422c0(0x42100())`, which tests `[0x119a590+0x10] == 5`. That is a state byte in a static singleton, cleared to 0 at its creation.
     - The singleton's functions (`0x421c0`, `0x422d0`, called from the level loader) send messages when the state is 5, and sit next to `CNetworkCrossPlayManager::Send warning %x`. It looks like the PS Vita cross-controller connection.
     - The code that sets 5 was not found. Offline it presumably stays below 5, so the thumbnail is a texture. This is not verified.
   - Jobs are queued by `0x401ab0` through `0x402560`. The half-screen capture `0x4026e0` passes format `2 * flag`, and both of its callers pass flag 1, so they ask for JPEG:
     - `0x702f96`, in `0x702c80`, the Popit capture update reached from `0x5b0d`. It sits among the cues `poppet/photograph_object`, `poppet/menuselect`, `poppet/switchbackground` and `poppet/error_3d`. The function switches on a capture mode at `[this+0x278]`:
       - Mode 2 calls `0x402f80` (Capture Object, format 0: texture).
       - Mode 1 builds a list of pointers taken from `[[this+0x10]+0x10]` (most likely the people in the photo) and then calls `0x4026e0`: the scene photo, JPEG.
       - Other modes go to the other `0x402f80` calls and to `0x403f50`. None of these is JPEG.
     - `0x52c7aa`, in `0x52c5c0`, called from the main loop at `0x5b31`. It sits next to the function that plays `poppet/photograph_object`. Which feature this is was not separated.
   - Width and height are half the globals at 0x1060a04/0x1060a08 (the render size), so 960x540 at 1080p.
   - Other captures are not JPEG. `0xd3240` (strings `poppet/camera_countdown` and `poppet/photograph_object`) calls `0x402e30` with no thumbnail argument (`edx = 0` at `0xd33d8`), so its format is 0. Capture Object's own call (`0x403a5f`, format 0) and the 320x176 capture in `0x403f50` (format 1) are not JPEG either.
2. **`0x401d50`**, a synchronous version of the same switch. Its format-2 branch is at `0x401e01`/`0x401ecd`. Its only caller, `0x403f50`, passes format 1, so this JPEG branch is not reached.
3. **`0x4043e0` (quality 70): the grief-report screenshot.** It is built next to the `<griefReport>`, `<jpegHash>` and `/grief` XML in `0x3359c0`. Online only.
4. **`0x404840` (quality 50): a PlayStation Camera frame.** The frame comes from `0x8778a0`, which calls `sceCameraGetFrameData` (strings `WebCamJob`, `Camera`). The code rotates each pixel's bytes before encoding. It needs a camera.

### How the game fills the encoder's structures (`0x412450`)

- **QueryMemorySize**: `{size = 8, attr = 0}`. A result `<= 0` counts as failure.
- **Create**: `(param, memory, size, &handle)`. The game allocates exactly the size QueryMemorySize returned. A nonzero result counts as failure.
- **Encode**: the param block is at `rbp-0x70` (0x30 bytes), and the third argument (output info) is NULL. The game stores `eax` in `*outSize` only if it is greater than 0, so **Encode returns the JPEG's length in bytes**. shadPS4's JpegEnc returns 0, which would give the game an empty photo.

  | Offset | Field | Value from the game |
  |---|---|---|
  | +0x00 | image | frame pixels |
  | +0x08 | jpeg | buffer of width * height bytes (one byte per pixel) |
  | +0x10 | imageSize | 4 * width * height |
  | +0x14 | jpegSize | width * height |
  | +0x18 | width | width |
  | +0x1c | height | height |
  | +0x20 | pitch | 4 * width |
  | +0x24 | pixelFormat (u16) | 1 (B8G8R8A8 in shadPS4's enum) |
  | +0x26 | mode (u16) | 0 (normal) |
  | +0x28 | colorSpace (u16) | 1 (YCC) |
  | +0x2a | sampling (u8) | 2 (4:2:0) |
  | +0x2b | quality (u8) | 70, 90 or 50 |
  | +0x2c | restartInterval | 0 |

- **Delete**: called whether or not Encode succeeded.

The game then reads the photo back through its texture loader, `0x276920`. That loader sniffs the first bytes: `89 PNG` is PNG, `FF D8 FF E0` or `FF D8 FF EE` is JPEG (decoded by the linked libjpeg, `0x412100`), `TEX ` is TEX, `DDS ` is DDS and `GTF ` is GTF. So the encoder's output must start with the JFIF APP0 marker. stb_image_write writes exactly that, and the harness checks it.

## Decoder: who calls it

`0x90f100(out texture, path)` opens the path with `sceKernelOpen`, reads the whole file with fstat/read, decodes it, and turns the pixels into a texture with `0x411d10` (DXT1). Its three callers all take paths that come from content search:

- **`0xa0d41`**, in `0xa0c10`. That function drains a queue at `obj+0x8c28`, which `0xa1030` fills from `0x90e2f0`. `0x90e2f0` runs `sceContentSearchInit`, `GetMyApplicationIndex` and `SearchContent` on `obj+0x8808`. `0xa0c10` is driven from the frontend state function `0x9f940` (strings `/LITTLEBIGPLANETPS3_XML/gameState`, `MyPlaylists`, `CommunityMenu`, ...).
- **`0xa14e0`**, in `0xa1440`, which calls `0x90e2f0` itself and decodes each result.
- **`0x5e0740`**, in `0x5e04b0`. It takes paths from content items through `0x5d7a60`, the same accessor `0xa1030` uses on the search results. It is called from a handler `0xc1a8a0` that is registered next to the pod's `pod/pod_select` sounds. That these items come from content search is an inference from the shared accessor; their creation was not traced.

With content search stubbed, `SearchContent`'s hit count stays at the 0 the game set before the call, so the first two callers decode nothing.

Whatever the third caller passes, `0x90f100` gives up before reaching the decoder unless `sceKernelOpen` and `sceKernelFstat` succeed on the path.

"My application index" suggests the search is for this game's screenshots in the capture gallery.

### How the game fills the decoder's structures (`0x90f100`)

**ParseHeader** gets the param block at `rsp+0xb0` and the info block at `rsp+0x148`. The game **reads `info+0x14` and allocates from it before testing ParseHeader's result**, so the implementation clears the info block first. Otherwise the game would read stack garbage. A nonzero result means failure.

Param block (0x10 bytes):

| Offset | Field | Value from the game |
|---|---|---|
| +0x00 | jpegData | file contents, 128-aligned |
| +0x08 | jpegSize (u32) | file size |
| +0x0c | option0 (u16) | 0 |
| +0x0e | option1 (u16) | 1 |

The game copies option0 and option1 unchanged into Decode's +0x24/+0x26. Their meaning was not established. A decode mode and a downscale factor where 1 means full size would fit: the game sizes its buffer from the full image size. The implementation always decodes at full size, and logs once if a caller passes anything other than 0/1.

Info block (0x24 bytes in OpenOrbis, `_types/jpeg.h`). The next local starts at +0x28, so it is no larger than 0x28. The game reads:

| Offset | Field | How the game uses it |
|---|---|---|
| +0x00 | width | output buffer is width * height * 4 (32-bit multiply), 128-aligned |
| +0x04 | height | |
| +0x14 | extraBufferSize | size of an extra buffer passed to Decode at +0x10/+0x20; the game skips the allocation when it is 0 |
| +0x18 | createAttr | copied into Create's `attr` |
| +0x1c | outputWidth | copied into Create's `maxWidth` |

OpenOrbis names +0x1c/+0x20 the output width and height. The extra buffer matches the Vita's `sceJpegArmDecodeMJpeg(..., pCoefBuffer, coefBufferSize)`. The fields from +0x08 to +0x13 are colour space and sampling factors in OpenOrbis; the game never reads them, and the implementation leaves them 0.

**QueryMemorySize** gets `{size = 0xc, attr = info+0x18, maxWidth = info+0x1c}` at `rsp+0xa0`. **Only 0 counts as failure**, so a negative error would become a huge allocation that fails, and Create would then get `(NULL, 0)`. Create therefore refuses a null memory pointer.

**Create** gets `(param, memory, size, &handle)` and must return 0.

**Decode** gets the param block at `rsp+0x68` (0x30 bytes, matches OpenOrbis' `OrbisJpegDecDecodeParam`, which names +0x2a `alpha`), and its third argument is NULL. **`eax > 0` means success.** On success the game builds a DXT1 texture from the buffer with pitch 4 * width.

| Offset | Field | Value from the game |
|---|---|---|
| +0x00 | jpegData | file contents |
| +0x08 | image | width * height * 4 buffer |
| +0x10 | extraBuffer | the info+0x14 buffer, or 0 |
| +0x18 | jpegSize | file size |
| +0x1c | imageSize | width * height * 4, or 0 if the allocation failed |
| +0x20 | extraBufferSize | info+0x14, or 0 |
| +0x24 | option0 (u16) | copied from the parse block |
| +0x26 | option1 (u16) | copied from the parse block |
| +0x28 | pixelFormat (u16) | 0 |
| +0x2a | alpha (u16) | 0xff |
| +0x2c | pitch (u32) | 0 |

**Delete** follows.

Two pieces of evidence settle the pixel format, pitch and return value:

- **The pixel order.** The decoded buffer and the screen capture both go through `0x411d10`, then the DXT packer `0x92d550`. The packer's last argument selects a byte order, and the two callers pass different values:
  - The screen capture passes 1 (from `0x401760`'s `[rsp+0x40] = 1`). The packer reads bytes 2, 1, 0, 3, so it swaps the capture's B8G8R8A8 to R, G, B, A.
  - The JPEG decode passes 0 (`0x90f100`'s `[rsp+0x40] = 0`). The packer reads bytes 0, 1, 2, 3 unchanged.

  So the decoder must deliver **R8G8B8A8**, which is what pixelFormat 0 means in Sony's numbering (shadPS4's PngDec and JpegEnc enums: 0 = R8G8B8A8, 1 = B8G8R8A8).
- **The tail of the block.** libScePngDec's decode parameters end with the same three fields, `{u16 pixel_format; u16 alpha_value; u32 image_pitch}` (shadPS4 `pngdec.h`), where pitch 0 means width * 4. The decoder follows the same convention.

## Implementation

`runtime/src/hle/jpeg.cpp`:

- **stb setup.** stb_image (decode) and stb_image_write (encode) come from `third_party/shadPS4/externals/stb`, built static inside `jpeg.cpp` (`STB_IMAGE_STATIC`, `STB_IMAGE_WRITE_STATIC`, `STBI_ONLY_JPEG`, no stdio). Their warnings are silenced only around the includes. stb's allocations are zeroed (`STBI_MALLOC` is calloc), so a truncated progressive file can't bring old heap contents into the image.
- **Handles.** QueryMemorySize returns 0x800, the same minimum shadPS4's JpegEnc uses. Create keeps a 16-byte header, 32-byte aligned and pointing at itself, inside the memory the game gave it, as shadPS4's JpegEnc does. Encoder and decoder handles carry different magic values. Delete clears the header. stb's working memory comes from the host heap, so nothing is left to free.
- **ParseHeader** walks the marker segments from SOI to the first SOS, bounds-checked against jpegSize.
  - It refuses a frame header with no scan after it: stb returns an image no scan ever wrote for such a file.
  - It refuses with "unsupported" the frames stb can't decode: lossless, arithmetic, 12-bit samples, 2 components, height 0 (DNL), and widths over 32767. The width limit keeps Decode's `(w << 16) | h` positive.
  - On success it fills width and height and sets extraBufferSize = 0, createAttr = 0, and outputWidth/outputHeight equal to the image size.
- **Decode** checks, in order: the handle; the addresses and sizes; pixelFormat 0 or 1; the header (same parser); width <= the handle's maxWidth; pitch >= 4 * width; and `pitch * (h - 1) + 4 * w <= imageSize`, computed in 64 bits. Only then does it decode with stb to RGB and write rows with the alpha byte. It writes nothing when a check fails.
  - It returns `(width << 16) | height`, as shadPS4's PngDec does. That is an inference; the game only needs a value greater than 0.
  - It fills the info block when the third argument is non-null.
- **Encode** runs shadPS4's parameter checks with 64-bit size arithmetic, converts the pixels to tight RGB (or grey for Y8), and encodes with stb at quality = the "compression ratio" byte, clamped to 1-100.
  - It handles R8G8B8A8, B8G8R8A8, Y8U8Y8V8 (JFIF full-range conversion) and Y8.
  - The output goes through a callback capped at jpegSize. If the JPEG doesn't fit, it returns an error without writing past the end.
  - On success it returns the length and fills the output info if one is given.
- **Error codes.** JpegEnc's are shadPS4's: 0x80650101 to 0x80650104. JpegDec's are inferred and need no confirmation, because the game only tests for zero or the sign. PngDec numbers its errors 0x806900xx next to its encoder's 0x806901xx, so the decoder's are taken as 0x806500xx with PngDec's suffixes: 01 address, 02 size, 03 param, 04 handle, 10 invalid data, 11 unsupported data, 12 decode error.
- **Logs** go to stderr, the first 16 of each kind and then one in 256:
  - `[jpeg] encoded WxH BGRA, quality Q -> N bytes (buffer B)`
  - `[jpeg] decoded WxH (N bytes, C components) to RGBA, pitch P`
  - `[jpeg] <function>: <reason> -> <code>`

### Differences from what Sony's library probably does (none matter to this game)

- stb always writes a 3-component YCbCr JPEG, even for Y8 input, where the chroma is neutral.
- stb subsamples chroma 4:2:0 up to quality 90 and not above, whatever the sampling field says (the game always asks for 4:2:0).
- stb writes no restart markers, and the MJPEG mode still writes Huffman tables.
- Nothing is downscaled.
- Whether the "compression ratio" byte rises with quality or with compression is not confirmed. The game uses 90 for its small thumbnail and 50 for camera frames, which fits quality, and that is how it is used.

## Test harness (`build\jpegtest.exe [file.jpg ...]`)

The harness calls the API exactly as `0x90f100` and `0x412450` do:

- The third arguments are NULL.
- The info block is pre-filled with 0xCD, like the game's uninitialised stack.
- Buffers are sized from the return values and from ParseHeader, are 128-aligned, and have 256 guard bytes after each.

stb is linked into the harness separately, to write the test JPEGs and as a reference decoder. Run on 2026-11-10 with the 40 game JPEGs extracted to the session scratchpad (not the repo): **270 checks, 0 failed**, about 1.4 s.

**Decoding stb-written JPEGs (quality 95), RGBA** at 320x200, 1x1, 17x9, 641x3 and 1920x1080:
- The output is identical to stb's own decode.
- Against the source image, the mean error is 0.33-0.75, the maximum 1-10 and the PSNR 47.6-52.9 dB.
- In the flat colour patches the error is at most 1.
- The return value is `(w << 16) | h`, alpha is 0xFF everywhere, and extraBufferSize is 0.

**Formats.** BGRA output equals RGBA with R and B swapped. A red patch reads R > 240 in byte 0 for RGBA and in byte 2 for BGRA. An alpha of 0x80 is applied to every pixel. With pitch 4w+20, the padding stays untouched and the rows match. These are refused, with the buffer untouched:
- a buffer one byte short (0x80650002)
- pitch below 4w (0x80650003)
- pixel format 7 (0x80650003)

**Malformed input.** ParseHeader refuses every case below, never writes extraBufferSize, and breaks no guard:
- empty input; a PNG signature; SOI alone; SOI followed by EOI
- a scan before the frame; a segment longer than the file; a segment of length 1
- 12-bit samples; lossless SOF3; arithmetic SOF9
- width 0; height 0; 2 components; a frame header that is too short; width 40000
- 4 KB of random bytes, with and without SOI
- a frame with no scan

A full header whose scan has no tables and no data parses, and decodes to a 16x16 image of zeros without overrunning.

**Truncated and corrupted files.**
- Truncation: 864 prefixes of a 12442-byte file. The header ends at byte 607, and every shorter prefix is refused. Longer prefixes decode, because stb fills the missing rows. There were no overruns.
- Corruption: 300 copies with 1-8 random bytes changed. 276 decoded, 22 were refused at decode and 2 at the header. There were no overruns.

**Encoding as the game does** (B8G8R8A8, pitch 4w, buffer w * h):

| Size | Quality | JPEG size | Share of the w*h buffer | PSNR |
|---|---|---|---|---|
| 960x540 | 70 | 23325 bytes | 0.04 | 36.7 dB |
| 480x270 | 90 | 12209 bytes | 0.09 | 34.0 dB |
| 640x360 | 50 | 10084 bytes | 0.04 | 33.4 dB |
| 1920x1080 | 70 | 76041 bytes | 0.04 | 39.4 dB |
| 33x31 | 70 | 749 bytes | 0.73 | 40.0 dB |

- **Every output is byte-for-byte what stb writes from the source RGB**, which proves the BGRA unpacking exact.
- Every output starts `FF D8 FF E0` and ends `FF D9`.
- Each decodes back through the HLE decoder identically to stb's decode.
- 17x9 in the game's 153-byte buffer is refused (0x80650102): the tables alone take about 600 bytes. In 4096 bytes it encodes to 674 bytes.

**Other encoder inputs.**
- R8G8B8A8 with pitch 4w+64 gives the same bytes as stb.
- Y8 decodes back at 55.2 dB.
- Y8U8Y8V8 bands of red, green, blue and grey come back within 1 of the expected colours.
- A 600-byte buffer is refused, as is a buffer one byte shorter than the output; a buffer of exactly the output's size works. No guard was broken.
- These are refused: 4:4:4 BGRA, BGRA as grayscale, pitch below 4w, an image smaller than pitch * h, an unaligned image, a null image, pixel format 5, and width 0.

**Handles.**
- Both QueryMemorySize functions refuse a wrong size or attr.
- Create(NULL, 0) gives an address error, and 16 bytes of memory a size error.
- A handle created at an odd address lands 32-byte aligned inside the memory and works.
- These give a handle error: a decoder handle given to Encode, a null or junk handle, decoding after Delete, and a second Delete.
- A 64-wide image on a handle created with maxWidth 32 gives a param error.

**Real game JPEGs.** All 40 decode identically to stb at 640x360, and invite1.img at 457x257. Re-encoded at quality 70 as BGRA, they come back at 43.0-58.6 dB. The other 11 of the 51 JPEGs the scan found were not extracted: the extraction script printed and saved only the first 40 hits.

## In-game check (not run here; another agent owns the game window)

1. **Binding.** At startup the runtime prints, on stdout, `imports: ... N functions stubbed` and a `missing (N unique):` list. Before this branch the list had the lines `libSceJpegDec  5: sceJpegDecParseHeader ...` and `libSceJpegEnc  4: ...` (for example `analysis\run_stdout.txt`, lines 27-28). With this branch both lines should be gone, and the stubbed count should be 9 lower. This is also the only offline evidence that the decoder's imports bind.
2. **The photo.** In a level, open the Popit and take a photo of the scene. Within a few frames stderr should show `[jpeg] encoded 960x540 BGRA, quality 70 -> N bytes (buffer 518400)`.
   - The size is half the render size, so 960x540 at 1080p.
   - N should be well under the buffer: 20-80 KB for the harness's synthetic 960x540 and 1920x1080 frames.
   - The game does this four frames after the capture, on the main thread.
3. **If no line appears,** the photo mode used saves a texture. Try each Popit photo mode: Capture Object and the countdown capture are expected to give no line.
4. A `quality 90` line is not expected offline (see the +0x5a note above).
5. **The result.** The photo should then show up in the Popit's photos with the right colours: red stays red. That proves the colours and the JFIF header together, because the game decodes the photo with its own libjpeg.
6. A `[jpeg] Encode: ... does not fit` or `... refused ...` line would mean a problem.

No `[jpeg] decoded` line is expected in offline play.

## Open questions

- What `0x52c5c0`'s JPEG capture is: the second caller of `0x4026e0` with flag 1, next to `poppet/photograph_object`.
- The +0x5a thumbnail predicate (`[0x119a590+0x10] == 5`): what sets 5 was not found. It appears to be cross-play (PS Vita) state.
- The meaning of the decoder's option0/option1 (0/1) and of the info block's +0x08..+0x13 fields.
- JpegDec's real error codes and Decode's real success value: inferred, and the game only checks the sign.
- If content search is ever implemented, check the decoder against real PS4 screenshots (1920x1080 or 3840x2160 JPEGs) and progressive files. stb handles progressive files, but the harness can't make one.

## Sources

- The game's own code: the call sites and offsets above, from `analysis\eboot.elf`. The helper scripts were in the session scratchpad: `jpeg_calls.py`, `callers.py`, `upgraph.py`, `riprefs.py` and `rela.py`.
- shadPS4 `src/core/libraries/jpeg/jpegenc.{h,cpp}` and `jpeg_error.h`, for the encoder layout, checks and error codes. `src/core/libraries/libpng/pngdec.{h,cpp}` and `pngdec_error.h`, for the decode tail, pixel-format numbering, return value and error numbering.
- OpenOrbis-PS4-Toolchain `include/orbis/_types/jpeg.h`, for the decoder structure sizes and the `alpha` field name: https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/blob/master/include/orbis/_types/jpeg.h. Its `JpegDec.h`/`JpegEnc.h` prototypes give QueryMemorySize four arguments, which the game's single-argument calls contradict.
- vitasdk `psp2/jpegarm.h`, for the coefficient buffer analogy: https://docs.vitasdk.org/jpegarm_8h.html.
