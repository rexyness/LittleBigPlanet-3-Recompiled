# Milestone 2: the HLE libraries checked against the console's own modules

This note compares the three new HLE libraries (text entry, JPEG, system dialogs) with the decrypted system modules in the user's firmware dump (`D:\Recomps\ROMs\PS4\Firmware Dump\`). The code compared is the `m2/merge` worktree `D:\Recomps\lbp3-wt\probe` at `91523ad`. Nothing in the worktree or the dump was changed.

## Method

- **Copies.** The ten modules were copied to `D:\Recomps\lbp3-recompiled\analysis\ghidra_fw\mods\` (gitignored).
  - `scripts\prep.py` patches each copy's `e_type` (0xFE18) to ET_DYN, turns the PT_SCE_RELRO header into PT_LOAD so Ghidra maps it, and applies the R_X86_64_RELATIVE and local-symbol relocations.
  - It writes `<module>.syms`: the exports from the dynlib symbol table (segment 0x61000000, tags 0x61000035/0x61000039), and the PLT thunks named from their JUMP_SLOT relocations. Names come from shadPS4's aerolib, re-hashed with `ps4_nid.py`.
- **Ghidra.** The project is `analysis\ghidra_fw\proj` (project `fw`). `scripts\LabelDump2.java` labels the symbols and writes every function's C to `analysis\ghidra_fw\out\<module>.c`.
- **Addresses.** Ghidra loads each module at 0x100000. **Every address below is a module offset**, so `libSceJpegEnc+0x190` is `0x00100190` in the .c files.
- **Unnamed exports.** Some libSceCommonDialog exports have no name in aerolib and keep their NIDs. Their roles are clear from their code:
  - `afLdI6i0lQw`: checks a dialog's base param.
  - `yxjgDvqUbGQ`: returns a client's construction error.
  - `reTFEla4NQw`: `Client::open`.
- **SDK branches.** The modules branch on `sceKernelGetCompiledSdkVersion`. LBP3 was built with SDK 2.00, so this note follows the branches for 0x0200xxxx: at least 1.50, 1.70 and 2.00, and below 2.50.
- **What the modules can't show.** The dialog modules are clients. Each one sends its parameters to the system shell (NPXS22010 for the common dialogs, an IPMI server for the IME) and copies back what the shell answers. **Which button was pressed, the end status and the result codes the shell produces aren't in these modules.** That is stated where it matters, rather than guessed.

## What the firmware settles

| Authors' guess | Firmware | Evidence |
|---|---|---|
| JpegDec error codes 0x806500xx with PngDec's suffixes | **Confirmed**: 01 address, 02 size, 03 param, 04 handle, 10 invalid data, 11 unsupported, 12 decode error. The firmware also has 05 (coefficient buffer missing or too small) and 06 (handle too small for the image). Its internal 0x80650039 (end of data) is reported as 12. | JpegDec+0x110..0x880 |
| Decode returns `(w << 16) \| h` | **Confirmed.** It is the *output* size after downscaling. It is 0 if either dimension is 0x8000 or more, and that case needs a non-null info pointer. | JpegDec+0x490 |
| Decoder pixel format 0 = R8G8B8A8, 1 = B8G8R8A8 | **Confirmed** from the colour converter's shuffle masks | JpegDec+0x3b90, table +0x7ec0 |
| ParseHeader output layout (+0x00 w, +0x04 h, +0x14 extra buffer, +0x18 attr, +0x1c/+0x20 output size) | **Layout confirmed.** The values differ: attr is 1 or 2, not 0, and +0x08..+0x13 hold the colour type, component count, component ids and sampling factors. | JpegDec+0x3c0 |
| Decoder option0/option1 | option0 (0 or 1): bit 0 loads the standard Huffman tables when a scan has none (Motion JPEG). option1 is a downscale divisor (1, 2, 4 or 8). Any other value is refused with 0x80650003. | JpegDec+0x280, +0x490, +0x27e0, +0x1340 |
| Encode returns the JPEG's length | **Confirmed** | JpegEnc+0x190, +0xab0 |
| The encoder's +0x2b byte is a quality | **Refuted.** It is a compression ratio: each quantiser is `base * r / 128` (`2r - 128` above 128), so a higher value compresses more. 128 gives the standard Annex K tables. | JpegEnc+0x510 |
| Encoder pixel format 1 = B8G8R8A8 | **Confirmed** (luma weights applied to byte 2, 1, 0) | JpegEnc+0x1870, tables +0x4600 |
| `sceCommonDialogInitialize` returns 0x80B80002 on a second call | **Confirmed** | CommonDialog+0xf20 |
| SaveDataDialog result and buttonId semantics | **Shell-side.** The module copies `result` and `buttonId` from the shell's finish data. Its only change is SDK-gated: from 4.50 it zeroes `buttonId` when `result` is 1 or negative. For SDK 2.00 the shell's values pass through. | SaveDataDialog+0x9cf0 |
| ImeDialog counts as a common dialog on a PS4 (`dialogs.md`, open questions) | **Refuted.** libSceImeDialog imports IPMI and nothing from libSceCommonDialog. It never registers a `CommonDialogUtil::Client`, so it never makes `sceCommonDialogIsUsed` true or gets BUSY from it. Keeping its state separate from MsgDialog and SaveDataDialog is right. | ImeDialog import list |
| SlowLoadProfile's Open without Initialize fails with NOT_INITIALIZED | **Confirmed.** It gets 0x80B80003, after the base-param and size checks. | SaveDataDialog+0xb2f0 |
| IME codes 0x80BC0105 (not running), 0x80BC0106 (not finished) and 0x80BC0016 (bad max length) | **Not confirmed by the module.** 0x80BC0105 and 0x80BC0016 don't appear in the client at all. 0x80BC0106 appears only for the disconnected state. Elsewhere these answers come from the shell. | ImeDialog .c, constant search |
| shadPS4-derived layouts | **No offset errors found** in the ImeDialog param and result, the keyboard param, the MsgDialog, SaveDataDialog (param, items, result, close and progress params) and GameCustomDataDialog params and results, or the JPEG blocks. Two details: SaveDataDialogParam +0x80 is a pointer used by modes 6 and 7, where ours has reserved bytes; SaveOptionParam +4 is an int that must be 0. Sizes match. | the validators cited below |
| GameCustomDataDialog result at +0x0C | **Confirmed as a field.** GetResult writes +0x08 = the common-dialog result, which it also returns, and +0x0C = a dialog-specific status from the shell. The value a cancel produces at +0x0C is shell-side. | GameCustomDataDialog+0x21b0, +0x9c0 |

## libSceJpegEnc

### sceJpegEncQueryMemorySize (+0x100)

```c
if (param == NULL) return 0x80650101;
if (param->size == 8) { if (param->attr == 0) return 0x800; return 0x80650103; }
return 0x80650102;
```

Ours (`jpeg.cpp:415`) does the same.

### sceJpegEncCreate (+0x130)

**The real module checks in this order:**
1. param NULL: 0x80650101.
2. size not 8: 0x80650102.
3. attr not 0: 0x80650103.
4. memory NULL: 0x80650101.
5. memorySize below 0x800: 0x80650102.
6. handle out NULL: 0x80650101.

Then the handle is `align32(memory)`, with `[0] = self` and `[8] = 8`.

**Ours** (`jpeg.cpp:424`) checks the three pointers first, then size and attr. It also checks that the aligned header fits. A NULL memory with a bad size gives a different code. The game never does that.

### sceJpegEncEncode (+0x190)

**Checks, in order:**

| Check | Code |
|---|---|
| Handle NULL, not 32-aligned, or `*h != h` | 0x80650104 |
| param NULL, image NULL, image not 4-aligned (unless pixel format 11), jpeg NULL | 0x80650101 |
| imageSize 0 or jpegSize 0 | 0x80650102 |
| Width or height outside 1..65535 | 0x80650103 |
| pitch 0, over 0x0FFFFFFF, not 4-aligned (unless format 11), `h * pitch` over 0x7FFFFFFF or over imageSize | 0x80650103 |
| Pixel format not in {0, 1, 10, 11} | 0x80650103 |
| mode over 1 | 0x80650103 |
| colorSpace not 1 or 2 | 0x80650103 |
| sampling over 2 | 0x80650103 |
| restartInterval outside -1..0xFFFF | 0x80650103 |

**Pixel-format rules:**
- Formats 0 and 1 need `pitch / 4 >= width`.
- Format 10 needs `pitch / 2 >= even(width)`.
- Format 11 needs colorSpace 2 and `pitch >= width`.
- After that, colorSpace 1 (YCC) needs a sampling other than 0, and colorSpace 2 (grey) needs sampling 0.
- **So grey output from RGBA, BGRA or YUYV input is allowed.**

```c
if (uVar3 < 2) { if (pitch >> 2 < width) goto fail; }
...
LAB_0010032f: if (colorSpace != 1) goto joined;  /* grey: sampling must be 0 */
              if (sampling == 0) goto fail;       /* YCC: 4:2:2 or 4:2:0 only */
```

**The output buffer.** The writer is set up at +0x430, which refuses `jpegSize < 0x400` with 0x80650102 before any work. Running out of room while encoding returns **0x80650110** (+0x10e0, +0x11d0).

**The result.**
- The driver at +0xab0 returns `end - start`, the JPEG's length.
- Encode returns that length.
- With a non-null third argument it writes `{size, height}`.
- On failure, with a third argument, it writes `{jpegSize, rows done}` and returns the error.

**The stream (+0x510):**
- SOI and `FF E0 00 10 "JFIF" 00 01 02 00 00 01 00 01 00 00`: version 1.02, no units, density 1:1. With mode 1 the APP0 says `"AVI1"` and no DHT is written (Motion JPEG).
- DQT. 0x84 bytes holds two tables; 0x43 holds one, for grey.
- SOF0, with sampling 0x22 for 4:2:0 and 0x21 for 4:2:2. Grey is one component.
- DRI, when a restart interval is set. -1 means one restart interval per MCU row (+0x460).
- The four standard DHT tables, or two for grey.
- SOS.

**The quantiser scale (+0x510; checked in the disassembly at +0x5f0..+0x67a):**

```c
iVar8 = (q < 2) ? 0x98 : 0x40;            /* rounding */
iVar6 = (q < 0x81) ? q : q * 2 - 0x80;
qt[i] = clamp(1, 255, (base_zigzag[i] * iVar6 + iVar8) >> 7);
```

- `base` (+0x3d10) is the Annex K luminance table and +0x3d50 the chrominance table, both in zig-zag order.
- So 128 gives the standard tables, 0 or 1 gives all ones, and larger values compress more.
- The game's 70, 90 and 50 give 0.547x, 0.703x and 0.391x the standard tables. In IJG/stb terms that is quality 73, 65 and 80.

**Colour conversion (+0x1870).** The shuffle-mask table at +0x4600 is indexed by `pixelFormat & 1`. For format 0 the luma weights 9794/19222/3752 (0.299/0.587/0.114 × 32768) apply to bytes 0, 1 and 2. For format 1 they apply to bytes 2, 1 and 0. So 0 = R8G8B8A8 and 1 = B8G8R8A8.

**Ours** (`jpeg.cpp:437`, `ValidateEncode` at `jpeg.cpp:198`):
- It returns the length, as the firmware does.
- **It passes the +0x2b byte to stb as an IJG quality** (`jpeg.cpp:458`). That inverts the scale:
  - 70 gives stb quality 70, about 10% coarser than the firmware (luma table sum 2211 against 2019).
  - 90 gives a much finer thumbnail (736 against 2591).
  - 50 gives much coarser camera frames (3688 against 1437).
  - With `Q = 100 - 50*s` for `s = r'/128 <= 1` (`Q = 50/s` above), stb's tables come within 1 of the firmware's at 50, 70 and 90 (checked numerically).
  - stb drops chroma subsampling above Q 90. Under this mapping that happens only for ratios below about 26, so the game's 50, 70 and 90 keep 4:2:0.
- **Smaller differences, none reachable by LBP3** (BGRA, YCC, 4:2:0, mode 0, restart 0, third argument NULL, `jpegSize = w*h`):
  - It refuses grey output from RGBA or YUYV (`jpeg.cpp:217`, `jpeg.cpp:222`).
  - It accepts restartInterval values below -1.
  - It has no 1 KiB minimum on jpegSize (`jpeg.cpp:201`).
  - It reports an overflow as 0x80650102, not 0x80650110 (`jpeg.cpp:463`).
  - It writes nothing to the output info on failure.
  - It always writes JFIF and DHT, even in mode 1.
  - It ignores 4:2:2 and the restart interval, and writes grey as three components.

### sceJpegEncDelete (+0x410)

It checks the handle (0x80650104) and clears `[0]`. Ours does the same and also clears its magic.

## libSceJpegDec

The decoder is fully in the module; it imports only libc. The game never reaches it offline (`jpeg.md`).

### sceJpegDecQueryMemorySize (+0x110)

```c
if (!p) return 0x80650001;
if (p->size != 0xc) return 0x80650002;
if (p->attr - 1 < 2 && p->maxWidth - 1 < 0xffff) {
    w = align32(p->maxWidth) | 0x10;
    return 0x7360 + w * 6 + (p->attr == 2 ? w * 0xa0 : w * 0x30);
}
return 0x80650003;
```

**attr must be 1 or 2.** For maxWidth 1920 and attr 1, w = 1936 and the result is 0x7360 + 1936×6 + 1936×0x30 = 0x20BC0 bytes.

**Ours** (`jpeg.cpp:320`) returns 0x800 and doesn't check attr or maxWidth.

### sceJpegDecCreate (+0x180)

**The real module:**
1. Checks the same three things as QueryMemorySize (size, attr, maxWidth).
2. memory NULL: 0x80650001.
3. memorySize below the computed size: 0x80650002.
4. handle out NULL: 0x80650001.

The handle is `align32(memory)`, with `[0] = self`, `+8 = 0xc`, `+0xc = attr`, `+0x10 = align32(maxWidth)` and `+0x14 = MCU-buffer capacity`.

**Ours** (`jpeg.cpp:328`) checks only `maxWidth != 0` and a 0x800 minimum.

### sceJpegDecParseHeader (+0x280)

**Checks:**
- param NULL or jpegData NULL: 0x80650001.
- jpegSize 0: 0x80650002.
- option0 over 1, or option1 not in {1, 2, 4, 8}: 0x80650003.
- info NULL: 0x80650001.

Then it parses with flags `option0 + 8` (bit 3: header only) and fills `info` through +0x3c0. **On failure `info` is not touched.**

**Parse errors** (+0x10b0 markers, +0x1e30 SOF):
- Fewer than 4 bytes, or no SOI first: 0x80650010.
- These frames are unsupported (0x80650011):
  - SOF3, SOF5..15, DAC and TEM;
  - precision other than 8;
  - width or height 0;
  - components outside 1..4;
  - H or V outside 1..4, or an H*V sum over 10;
  - `w*h >= 2^29`, or `2^28` for progressive;
  - three-component mixes of 3 with 2 or 4 sampling.
- A truncated segment, no SOS, or EOI before SOS: 0x80650012 (EOI comes in as 0x80650039 and is mapped).

**The info block (+0x3c0):**

| Offset | Value |
|---|---|
| +0x00 / +0x04 | width / height |
| +0x08 (u16) | colour type: 1 YCbCr (3 components with JFIF APP0, or ids 1, 2, 3), 2 grey (1 component), 0 anything else (CMYK, RGB ids) |
| +0x0a (u16) | component count |
| +0x0c..+0x0f | component ids |
| +0x10..+0x13 | sampling `H<<4 \| V` per component |
| +0x14 | coefficient-buffer size: `blocks * 128 + 0x40` for progressive files, 0 for baseline |
| +0x18 | createAttr |
| +0x1c / +0x20 | `ceil(w / option1)` / `ceil(h / option1)` |

createAttr is:
- 0 for colour type 0;
- 1 for grey;
- for YCbCr, `sumHV * (96 / (8 * maxH)) < 37 ? 1 : 2`. That is 1 for 4:4:4, 4:2:2 and 4:2:0.

```c
if (hdr[1] == 0) attr = 0; else if (hdr[1] == 2) attr = 1;
else attr = 2 - (sumHV * (0x60 / (maxH << 3)) < 0x25);
```

**Ours** (`jpeg.cpp:305`):
- It clears `info` before any check (`jpeg.cpp:308`). That is safer than the firmware for the game, which reads `info+0x14` before testing the result.
- It leaves +0x08..+0x13 zero.
- It sets **createAttr 0** (`jpeg.cpp:182`). Real QueryMemorySize and Create would refuse that.
- It reports extraBufferSize 0 even for progressive files.
- It ignores the options, so it never downscales.
- It uses 0x80650010 where the firmware uses 0x80650012 (truncation, EOI before scan).
- It refuses widths over 32767 (`jpeg.cpp:168`).

### sceJpegDecDecode (+0x490, output set-up +0x6e0)

**Checks, in order:**

| Check | Code |
|---|---|
| Handle | 0x80650004 |
| param, jpegData or image NULL | 0x80650001 |
| image not 4-aligned for formats 0, 1, 9 and 10 (mask table at +0x79c0) | 0x80650001 |
| jpegSize or imageSize 0 | 0x80650002 |
| option0 over 1, option1 not in {1, 2, 4, 8} | 0x80650003 |
| Pixel format not in {0, 1, 9, 10, 11} | 0x80650003 |
| alpha over 0xFF | 0x80650003 |
| Parse errors | as ParseHeader |
| Progressive file with the extra buffer NULL or too small | 0x80650005 |
| info NULL when the output is 0x8000 or more | 0x80650001 |

**The output set-up (+0x6e0):**
- Colour type not 1 or 2: 0x80650011.
- Row bytes: `4*w` for formats 0 and 1, `2*even(w)` for 9 and 10, `w` for 11.
- **pitch 0:** an imageSize below `h * row` gives 0x80650002. A size of 2^31 or more gives 0x80650011.
- **pitch not 0:** if it isn't a multiple of 4 (formats 0, 1, 9, 10), is below the row, `h*pitch > 0x7fffffff`, or imageSize is below `h * pitch`, the result is **0x80650003**.
- A handle whose `align32(maxWidth)` is below `align32(outW)`, or whose MCU capacity is too small (attr 1 handle, attr 2 image), gives **0x80650006**.

**The control word** is `option1<<24 | alpha<<16 | 0x8000`:

| Format | Bits added | Meaning |
|---|---|---|
| 1 | 0x4000 | R/B swap |
| 9 | 0x2000 | packed 4:2:2 |
| 10 | 0x6000 | packed 4:2:2, swapped |
| 11 | 0x0800 (word 0x8800) | Y8 |

So the words are 0x8000, 0xC000, 0xA000, 0xE000 and 0x8800 for formats 0, 1, 9, 10 and 11.

**The result.** Decode returns `CONCAT22(outW, outH)`. An internal 0x80650039 (data ended) still counts as success. With `info`, the info block is filled as in ParseHeader.

**Colour conversion (+0x3b90).** It uses JFIF full-range coefficients (Cr→R 22970×2 = 1.402, Cb→B 29032×2 = 1.772, chroma −128). The shuffle mask at +0x7ec0 is the identity for format 0 (bytes R, G, B, A) and swaps the halves for format 1 (B, G, R, A). Alpha is the alpha parameter's byte.

**Ours** (`jpeg.cpp:343`):
- It accepts only formats 0 and 1.
- It has no image-alignment check, and it clamps alpha (`jpeg.cpp:384`).
- It uses `pitch*(h-1)+row` and 0x80650002 for an explicit pitch (`jpeg.cpp:365`). It doesn't need the pitch to be 4-aligned.
- It gives 0x80650003 for a width over maxWidth (`jpeg.cpp:358`), where the firmware gives 0x80650006.
- It needs no coefficient buffer.
- It accepts colour type 0 (CMYK through stb).
- The pixel order, alpha fill and return value match.

### sceJpegDecDelete (+0x880)

It checks the handle (0x80650004) and clears `[0]`. Ours matches.

## libSceImeDialog

### sceImeDialogInit (+0xd00, checks at +0x1090, request built at +0x2f80/+0x39f0)

**Order:**
1. **An instance already exists: 0x80BC0001 BUSY (before the NULL check).**
2. param NULL: 0x80BC0031.
3. type over 4: 0x80BC0011.
4. Option bits outside the SDK mask: 0x80BC0015.
5. supportedLanguages bits outside the mask: 0x80BC0012, and the module prints "Not supported language". For SDK 2.00 the mask is 0x0011FFFFF. All 22 entries of the game's language table (eboot `0xe1e500`, 32-bit, indexed by system language 0..0x15) are inside it, using bits 0..20.
6. posx or posy negative, or at or above a limit (the table at +0x6d40 is chosen by option 0x4000, the 4K-coordinates flag): 0x80BC0018 / 0x80BC0019.
7. Horizontal or vertical alignment of 3 or more: 0x80BC001A / 0x80BC001B.
8. Multiline together with password, password with a type other than 1 or 4, or multiline with types 2, 3 or 4: 0x80BC0030.
9. userId not in `sceUserServiceGetRegisteredUserIdList`: 0x80BC0010.
10. Any reserved byte (+0x50..+0x5f) not zero: 0x80BC0032.
11. inputTextBuffer NULL: 0x80BC0017.
12. Checks on the extended param: 0x80BC001C.

**maxTextLength is not checked by the module.** It is clamped to 0x800 when the request is built:

```c
uVar5 = 0x800; if (uVar7 < 0x800) uVar5 = uVar7;   /* uVar7 = param->maxTextLength */
```

The prefill is copied up to that length, and also kept (`wcsncpy(..+0x5b8, buffer, 0x800)`). The shell's answer to the open request becomes Init's return value, so the shell *could* refuse 0. That isn't visible here.

**The param layout** (from the copies at +0x2f80) matches `ime_dialog.h`:

| Offset | Field |
|---|---|
| +0x04 | type |
| +0x08 | languages |
| +0x10 | enter label (byte) |
| +0x14 | input method |
| +0x18 | filter |
| +0x20 | option |
| +0x24 | max length |
| +0x28 | buffer |
| +0x30 / +0x34 | position |
| +0x38 / +0x3c | alignment |
| +0x40 | placeholder (64 units) |
| +0x48 | title (128 units) |
| +0x50 | 16 reserved bytes |

**Ours** (`ime_dialog.cpp:452`):
- It checks NULL before BUSY (`ime_dialog.cpp:455`).
- **It refuses `maxTextLength == 0 || > 2048` with 0x80BC0016** (`ime_dialog.cpp:461`).
- It checks nothing else: user, alignment, option combinations, reserved bytes.

### sceImeDialogGetStatus (+0x2370)

- No instance: 0.
- Local abort state (5): 2.
- Otherwise it polls the shell. The shell's 0, 1 and 2 are returned as they are, its 3 is returned as 1, and errors as 0.

So the statuses are NONE, RUNNING and FINISHED, as in ours. **When FINISHED comes is up to the shell.** After an Abort sent over IPC, the module sets state 3 and goes on polling. FINISHED arrives when the shell closes the dialog, possibly a few frames later. Ours reports FINISHED at once (`ime_dialog.cpp:550`).

### sceImeDialogGetResult (+0x2680)

**Checks:**
- No instance: 0x80BC0107.
- result NULL: 0x80BC0031.
- Any of the result's 12 reserved bytes not zero: 0x80BC0032.
- Disconnected state 4: 0x80BC0106.

**Local abort (state 5):** endStatus = 2, and the kept prefill is copied back into the game's buffer.

**Otherwise it sends two IPC calls:**
- Command 4 fills `endStatus`.
- **Command 5 copies the shell's text into the game's buffer (`(max+1)*2` bytes), whatever the end status.**

So the buffer is written at GetResult time, on cancel and abort too, with whatever the shell sends.

**Ours** writes the text when the status changes to FINISHED (`ime_dialog.cpp:426`), and only on OK. It doesn't check the reserved bytes.

### sceImeDialogAbort (+0x2660 → +0x4990)

- No instance: 0x80BC0107.
- Already in local abort: returns 0 with no change.
- Otherwise it sends command 3 and returns the shell's answer. If the IPC link is gone, it marks local abort and returns 0.

**Ours** returns 0x80BC0105 for a second Abort, or for an Abort after the dialog finished (`ime_dialog.cpp:542`). On a PS4 the shell answers that case.

### sceImeDialogTerm (+0x2700)

- No instance: 0x80BC0107.
- Unless the state is 3, 4 or 5, it sends command 6 (+0x5790) and returns the shell's answer. A negative answer keeps the instance.
- Then it frees the instance.

**Ours** returns NOT_FINISHED while running. On a PS4 that answer is the shell's.

## libSceIme (keyboard)

**sceImeKeyboardOpen (+0x4fd0):**
- param NULL: 0x80BC0031.
- Handler (+0x10) NULL: 0x80BC0022.
- A keyboard already open: **0x80BC0001**.
- Invalid user: 0x80BC0010.
- Option bits: 0x80BC0015.
- Reserved bytes (+0x04..+0x07, +0x18..+0x1f): 0x80BC0032.
- IPC connection errors 0x80BC07BA/BB become **0x80BC0008**. LBP3 retries on that code (game `0x4bdd30`) and gives up on 0x80BC0003.

**sceImeKeyboardClose (+0x5b40):** none open: 0x80BC0002; user not open: 0x80BC0010.

**sceImeUpdate (+0xfb0):** 0x80BC0002 when neither an IME nor a keyboard is open with that handler. Otherwise it runs `sceImeKeyboardUpdate` (+0x6100), which delivers the shell's events.

**What the game does with the events.** Its handler at `0x4bde30` takes:

| Event | Meaning | What the game does |
|---|---|---|
| 0x100 | keyboard open | sets the "keyboard present" byte `0x13a4598` (read by `0x4bdd00`) |
| 0x101 / 0x103 | key down / repeat | queues characters |
| 0x104 | connection | sets the byte; polls every frame instead of every 10th |
| 0x105 | disconnection | clears the byte |
| 0x106 | abort | sets a reopen flag; the next update calls KeyboardOpen again, then Close once Update returns 0 |

- The module itself makes 0x105 and 0x106 only when the IPC link drops. 0x100 and 0x104 come from the shell.
- **Who reads the keyboard-present byte.** Its getter `0x4bdd00` has one direct caller, `0x3bc299`, inside `0x3bc0a0`. That function builds a list of input-device tags (`"ps4"`, `"pad"`, `"move"`, `"kbd"`, `"mouse"`, `"default"`) and adds `"kbd"` only when the byte is set.
- **Ours** (`ime_dialog.cpp:575`, `:581`, `:586`) always returns 0 and sends no events. So `"kbd"` is never added, as on a PS4 with no USB keyboard plugged in, and Update is polled every 10 frames.
- **Firmware codes ours never returns:** KeyboardOpen when one is already open (0x80BC0001), and Close or Update with nothing open (0x80BC0002). The game ignores all three. Its re-open path only runs after event 0x106.

## libSceCommonDialog

**sceCommonDialogInitialize (+0xf20):**

```c
uVar2 = 0x80b80002;
if (DAT_0010808c == '\0') {            /* not yet initialised */
    if (FUN_00101c90(shm) == 0) uVar2 = 0x80b8000e;
    else { DAT_0010808c = 1; ...; uVar2 = FUN_00100fe0(); }   /* launches NPXS22010 */
}
```

The first call returns 0, or a launch error; a missing shared-memory page gives 0x80B8000E. Every later call returns 0x80B80002. Ours (`common_dialog.cpp:558`) matches.

**sceCommonDialogIsUsed (+0xfb0)** is true while any `CommonDialogUtil::Client` is registered. That includes MsgDialog, SaveDataDialog and **GameCustomDataDialog** (each Initialize creates one; +0x1420, `clientRegister` +0xad0).

**The client state** (+0x68):

| State | Meaning |
|---|---|
| -1 | construction failed; error at +0x6c: 0x80B80001 if `sceCommonDialogInitialize` wasn't called (+0x380), 0x80B8000E with no shared memory |
| 1 | initialised (after `launchCmnDialog`, +0x1630) |
| 2 | running (`Client::open`, +0x1650) |
| 3 | finished (`updateState`, +0x1670, when the shell reports done) |

**`Client::open` accepts only state 1 or 3. In any other state it returns 0x80B80006** (+0x580):

```c
if ((state | 2) == 3) { ...copy param...; return 0; } else return 0x80b80006;
```

`closeModule` (+0x1710) returns 0x80B8000B unless the state is 2, and 0x80B8000C if a close was already requested. `getResult` (+0x1730) returns the shell's result word, which starts as 0x80B80007.

## libSceMsgDialog

- **Initialize (+0x5b0):**
  - own client already exists: 0x80B80004;
  - `sceCommonDialogIsUsed()`: 0x80B80008;
  - client construction error, for example 0x80B80001;
  - launch failure: 0x80B8000E.
- **Open (+0x650), in order:**
  1. NULL: 0x80B8000D.
  2. Base param (`afLdI6i0lQw`, +0x12f0: `size == 0x30`, `magic == addr + 0xC0D1A109`, 43 reserved bytes zero): 0x80B8000A.
  3. size not 0x88: 0x80B8000A.
  4. The mode-specific pointer checks: exactly one of the user, progress and system params.
  5. For mode 3, sysMsgType 0..6 and zero reserved bytes (+0x1300). Types 1, 2, 5 and 6 need `sceUserServiceIsLoggedIn(userId)`.
  6. Reserved bytes +0x5c..+0x83.
  7. No client: 0x80B80003.
  8. `Client::open` (running gives 0x80B80006).
- **UpdateStatus (+0x9b0)** and **GetStatus (+0xa30)** return 0, 1, 2 or 3 as ours.
- **GetResult (+0xa90):**
  - NULL: 0x80B8000D;
  - reserved bytes +0x0c..+0x2b not zero: 0x80B8000A;
  - no client: 0x80B80003;
  - not finished: 0x80B80005.
  - It copies 0x2c bytes and returns `result`. From SDK 3.00 a cancelled YES/NO becomes `{0, NO}`; for SDK 2.00 nothing changes. LBP3 never calls it.
- **Close (+0xd10):** no client: 0x80B80003; not running: 0x80B8000B.
- **Terminate (+0xdc0):** no client: 0x80B80003; otherwise it destroys the client in any state and returns 0.

**Ours** (`common_dialog.cpp:569..641`):
- It checks the state before the param.
- It gives BUSY 0x80B80008 for Open while running (`common_dialog.cpp:85`), not 0x80B80006.
- It gives 0x80B8000B for Close with no client, not 0x80B80003.
- It doesn't check reserved bytes, the user or the system-message type range.
- The game's calls (Initialize, Open mode 3 with types 0-2, UpdateStatus, Terminate) behave the same.

## libSceSaveDataDialog

**Initialize (+0xb110)** matches MsgDialog's: 0x80B80004, then 0x80B80008, then the client error, then 0x80B8000E. Its errors go through +0x7640, which maps 0x809F8004 and 0x809F8009 to 0x80B8000A and other outside codes to 0x80B8000E.

**Open (+0xb2f0):**
1. NULL: 0x80B8000D.
2. Base param: 0x80B8000A.
3. size not 0x98: 0x80B8000A.
4. No client: 0x80B80003.
5. A per-mode validator, then `Client::open` (state not 1 or 3: 0x80B80006).

**The common validator (+0x77a0):**
- mode 1..7; dispType 1..3.
- animParam values 0 or 1, with zero reserved bytes.
- **items is required in every mode.**
- Exactly the one sub-param that belongs to the mode is non-NULL:

| Mode | Offset |
|---|---|
| 2, user message | +0x50 |
| 3, system message | +0x58 |
| 4, error code | +0x60 |
| 5, progress bar | +0x68 |
| 6, 7 | +0x80 |

- optionParam (+0x78): back 0 or 1, zero reserved.
- +0x88..+0x97 zero.

**Per mode:**
- **LIST (+0x9430 → +0x8130):**
  - dispType LOAD or DELETE needs `dirNameNum > 0` and dirName.
  - dirNameNum ≤ 1024; each name non-empty, valid and unique.
  - newItem is checked for SAVE.
  - focusPos is 0..6; focusPosDirName only with focusPos 6.
- **USER_MSG (+0xa7a0), ERROR_CODE (+0x9230), PROGRESS_BAR (+0x9f10)** use the items check at +0x7d40, which needs focusPos 0 and no focusPosDirName. PROGRESS_BAR with dispType LOAD or DELETE needs `dirNameNum != 0`; LBP3's helper `0x90a150` sets it to `dirName != NULL`.
- **SYSTEM_MSG (+0xa1f0):**
  - type 1..15 (+0x8b10). Types 3, 4, 8 and 14 need dispType SAVE; types 9, 10, 11, 13 and 15 need SAVE or LOAD.
  - For SDK 2.00, types 1, 4, 8 and 14 need no dirName.
  - Types 9 and 13 need `userId != -1`, a non-empty dirName and no newItem.
  - The other types need dirName and dirNameNum to agree.
  - **So type 9 is a real system message type.** What it says is drawn by the shell.

**UpdateStatus (+0xb410) and GetStatus (+0xb530)** return 0, 1, 2 or 3.

**GetResult (+0xb620 → +0x7590 → +0x9cf0):**
- NULL: 0x80B8000D; no client: 0x80B80003; not finished: 0x80B80005.
- Reserved bytes +0x28..+0x47 not zero: 0x80B8000A.
- It copies `mode`, `result` and `buttonId` from the shell. From SDK 4.50 `buttonId` is set to 0 when `result` is 1 or negative.
- **It zeroes and fills `dirName` (32 bytes) and `param` (0x530 bytes, the chosen save's SceSaveDataParam) when they are given.**
- It returns `userData` and 0.

**Close (+0xb720):**
- no client: 0x80B80003;
- **param NULL: 0x80B8000D**;
- not running: 0x80B8000B;
- `anim` must be 0 or 1, and the 32 reserved bytes zero (+0x9020).
- It then sends the close request (+0x6f40). **The status stays RUNNING until the shell confirms.**

**Terminate (+0xb840):** no client: 0x80B80003; otherwise it destroys the client and returns 0.

**IsReadyToDisplay (+0xbb20):** no client: 0x80B80003; finished: 0x80B80006. Otherwise **0 until the shell sends its "ready" message, then 1** (+0x72e0).

**ProgressBarSetValue (+0xba10) and Inc (+0xb900):**
- no client: 0x80B80003; finished: 0x80B80006;
- target not 0: 0x80B8000A;
- mode not 5: 0x80B8000F (+0x7150);
- otherwise the value is clamped to 100 and sent, and the call returns 0.

**Ours** (`common_dialog.cpp:645..726`):

| Function | Difference |
|---|---|
| Open | Requires items only for LIST (`common_dialog.cpp:395`); none of the other checks above |
| Close | Ignores its param (`common_dialog.cpp:699`); changes to FINISHED at once (`:127`); returns 0x80B8000B with no client |
| GetResult | Leaves `param` alone |
| IsReadyToDisplay | Always 1, in any state (`:701`) |
| SetValue | 0x80B8000B when not running (`:708`) |

The game:
- zeroes the 0x530 param buffer before GetResult and never reads it afterwards. **All six GetResult sites were checked:** `0x9d08a` and `0x9d1a0` (SlowSaveProfile `0x9c710`), `0x90a91d` and `0x90ab72` (SaveLevel `0x90a570`), `0x90c99a` (SlowLoadProfile) and `0x97e66c` (LoadLevel). They read only `result`, `buttonId`, `dirName` and the return value's sign. The buffer is a stack local in a state handler that returns straight after;
- ignores SetValue's result (`0x9e764`);
- waits for `IsReadyToDisplay > 0`, with no timeout at `0x90c9bf`;
- after Close, spins until the status is NONE or FINISHED.

## libSceGameCustomDataDialog

**Initialize (+0x1fb0)** follows the same pattern as MsgDialog: 0x80B80004, **0x80B80008 if any common dialog client exists**, the client error (0x80B80001 without `sceCommonDialogInitialize`), then 0x80B8000E.

**Open (+0x2050):**
1. No client: 0x80B80003.
2. Its own check (+0x1790): NULL: 0x80B8000D; size not 0x90: 0x80B8000A; reserved +0x50..+0x8f: 0x80B8000A; data pointer (+0x48) NULL: 0x80B8000D; userId -1 or not logged in: 0x80B8000A; then per-mode string and size limits.
3. The base param.
4. The per-mode set-up (+0x540). That calls `sceNpGetOnlineId` and keeps its error for the shell; mode outside 1..3 gives 0x80B80009.
5. `Client::open`.

**GetResult (+0x21b0):**
- NULL: 0x80B8000D;
- reserved +0x18..+0x37 not zero: 0x80B8000A;
- no client: 0x80B80003;
- not finished: 0x80B80005.

Then the per-mode fill (+0x9c0) writes:
- +0x00: a 64-bit value from the mode object;
- **+0x08**: the shell's result word;
- **+0x0C**: the shell's dialog status;
- when +0x0C is 0, a list at `*(+0x10)` (count, then 0x14-byte entries).

Finally GetResult overwrites +0x08 with `Client::getResult()` **and returns it**, which is 1 for a user cancel:

```c
iVar2 = FUN_00100860(client, result);     /* +0x08, +0x0C, list */
if (iVar2 == 0) { uVar3 = Client::getResult(client); *(int*)(result + 8) = uVar3; }
return uVar3;
```

**Close (+0x2370):** no client: 0x80B80003; initialised or finished: 0x80B8000B. **Terminate (+0x2420):** no client: 0x80B80003; otherwise 0.

**Ours** (`common_dialog.cpp:734..767`):
- Initialize doesn't take part in the common-dialog lock and doesn't need `sceCommonDialogInitialize`.
- Open checks only NULL.
- GetResult writes only +0x0C = 1 and returns 0.

**The game** (`0xaddfe1`) zeroes 0x38 bytes, treats a negative return as the error code, and otherwise reads only +0x0C. So ours is read the same way as a firmware result whose +0x0C is 1. Whether a real cancel puts 1 at +0x0C is decided by the shell.

## Not visible in the modules

- The `result`/`buttonId` the shell reports for each SaveDataDialog and MsgDialog answer (Yes, No, Back, OK), and the text of system message type 9.
- When the IME shell reports FINISHED after an Abort; what text it returns on cancel; whether it refuses `maxTextLength == 0`.
- Whether the IME shell sends keyboard event 0x100 or 0x104 with no USB keyboard attached.
- GameCustomDataDialog's +0x0C value for a cancel.

**I looked for the shell side in the dump and didn't find it.**
- `libSceCdlgUtilServer.sprx` is the common dialogs' server plumbing. Its 2370 strings are shared memory, timestamps and logging, with nothing about save, message or button handling.
- `libSceImeBackend.sprx` is the kana-to-kanji conversion client.
- The common dialog application (NPXS22010) and its UI aren't in the dump, which holds `system/common/lib`-style modules and shared assemblies only.

So the points above stay open. A dump of the NPXS22010 application folder could answer them.

## Addendum: libSceErrorDialog (`dialogs.cpp`)

LBP3 imports `sceErrorDialogInitialize`, `Open`, `UpdateStatus` and `Terminate`. They are implemented in `runtime/src/hle/dialogs.cpp`. The firmware module is `libSceErrorDialog.sprx`, decompiled to `out\libSceErrorDialog.c`.

**It isn't a common dialog.** It talks to its own IPMI server (`SceErrorDlgServ`, +0x680) and has its own error range, **0x80ED00xx**. It never takes the common-dialog lock.

**Initialize (+0x440):** a second call returns 0x80ED0002. Otherwise the state becomes 1 and it returns 0.

**Open (+0x460), in order:**
1. NULL, `size != 0x10`, reserved (+0x0c) not 0, or **errorCode (+0x04) not negative**: 0x80ED0003.
2. **userId (+0x08) 0, or over 0xFF and outside 0x10000000..0x3FFFFFFF: 0x80ED0007.**
3. Not initialised: 0x80ED0001.
4. Already running (state 2): 0x80ED0005.
5. Errors from the server: 0x80ED0006 or 0x80ED0004.
6. On success the state becomes 2.

**UpdateStatus (+0x4c0, poll at +0x7e0)** returns the state, 0..3. The state becomes 3 when the server reports the dialog closed. **Close (+0x4b0):** not initialised: 0x80ED0001; it moves 2 to 3 and returns 0 otherwise. **Terminate (+0x450):** not initialised: 0x80ED0001; otherwise the state becomes 0 and it returns 0.

The param layout `{size, errorCode, userId, reserved}` matches ours.

**Ours** (`dialogs.cpp:69..115`) uses common-dialog codes instead of 0x80EDxxxx:

| Case | Ours | Firmware |
|---|---|---|
| Initialize again | 0x80B80004 | 0x80ED0002 |
| Open with NULL | 0x80B8000A | 0x80ED0003 |
| Open not initialised, or while running | 0x80B80003 | 0x80ED0001 / 0x80ED0005 |
| Terminate or Close with nothing open | 0 | 0x80ED0001 |

It also doesn't check size, the errorCode's sign or the userId.

**How the game uses it:**
- **`0x12080`** is called at the very end of `main` (`0xa8f`).
  1. It initialises and opens with the code recorded by `0x12070` (the fatal boot error, or -1 if none) and userId 0xFE.
  2. It then loops: **wait for status 3, then Open again.** It only gets out if an Open fails.
  3. So on a PS4 this is an error dialog that can never be dismissed. Ours behaves the same, since Open from FINISHED succeeds.
  4. Its text is wrong for this case, though: ours always says "couldn't connect to its online services", but this is a boot error.
  5. The port reaches this only if the game's main loop ends by itself. Closing the window calls `ExitProcess` (`presenter.cpp:209`).
- **`0x8c1c80`** is a per-frame state machine.
  1. It calls Initialize and Open and ignores both results.
  2. It waits for status 3, calls Terminate, then waits for status 0.
  3. Here the checks matter. **With a non-negative error code or userId 0, a PS4's Open fails and the machine waits at status 1 forever without showing anything.** Ours shows the box and finishes.
  4. Which codes reach this was not traced. It's in the NP code, so it is likely online-only.
- Every return value the game checks is only tested for its sign, so the different code numbers don't matter.

**Suggested, low priority:**
- Use the 0x80ED00xx codes.
- Refuse a non-negative errorCode and userId 0, as the firmware does.
- Word the message for the boot-error case (`0x12080`) as a general error, not a connection one.

## Addendum: commit `ac0ebfd` checked against the firmware

`ac0ebfd` on `m2/merge` applied three of these findings. All three match the firmware:

- **`jpeg.cpp`, `QualityFromRatio`:** `r < 2` gives 100. Otherwise `r' = r` up to 128 and `2r - 128` above, `s = r'/128`, and the quality is `100 - 50s` up to `s = 1` and `50/s` above, rounded and clamped to 1..100. That is the formula given above.
  - Its test values all check out: 0 and 1 give 100, 50 gives 80, 70 gives 73, 90 gives 65, 128 gives 50, and 255 gives 17 (`r' = 382`, `s = 2.98`).
- **`ime_dialog.cpp`:** a max length over 2048 is clamped, as the module does (`min(max, 0x800)` at +0x2f80). A length of 0 is still refused with 0x80BC0016. The client module has no such check (the shell might), and LBP3 never passes 0.
- **`common_dialog.cpp`, `sceGameCustomDataDialogGetResult`:** it writes 1 at +0x08 and +0x0C and returns 1. That matches `Client::getResult()` for a user cancel being both written at +0x08 and returned. The game still reads only +0x0C after a non-negative return, so it sees the same thing as before.

## Out of scope, noted

LBP3 also imports **libSceNpCommerceDialog** (Initialize, Open, UpdateStatus, GetResult, Terminate; calls at `0x343b8c..0x343e97` and `0x8c2a27..0x8c2ae6`). The runtime doesn't register it, so it is still a stub. On a PS4 it is a common dialog client too, so it takes the same lock as MsgDialog and SaveDataDialog. The firmware module (`libSceNpCommerce.sprx`) imports the `CommonDialogUtil::Client` constructor (`QXFsLON5QWw`), `sceCommonDialogIsUsed`, `launchCmnDialog` and `getResult`. Its functions were not examined.

## Files

- Copies, symbol lists, scripts, project and decompiled C: `D:\Recomps\lbp3-recompiled\analysis\ghidra_fw\` (`mods\`, `dyn\`, `scripts\prep.py`, `scripts\LabelDump2.java`, `proj\`, `out\*.c`).
- Game-side checks used `tools\ghidra_decomp.sh` on `0x90a91d`, `0x90c99a`, `0x4bddd7`, `0x4bde30`, `0x4be1a3` and `0x4be037`, and `tools\disasm.py` on `0xaddfe1`, `0x9e764` and `0x90a150`.
