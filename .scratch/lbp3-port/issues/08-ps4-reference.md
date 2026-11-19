# How does LBP3 look and play on a real PS4 where the port is in doubt?

Type: research
Status: resolved
Claimed by: research agent (gap-list session, 2026-11-09)
Findings: branch `research/ps4-reference`, file `docs/research/ps4-reference.md`
Blocked by: none
Map: [LBP3 native port](../map.md)

## Question

Three things the port can't settle by itself. Answer each from real-PS4 sources: gameplay videos with timestamps, the game's manual, wikis and community guides.

1. **The Create-mode sky.** On a real PS4, what colour is the sky or backdrop of a new level made from My Moon → Create!? On the port it's flat yellow-beige, and upstream thinks it "should probably be blue" (`NOTES.md:663-664`). The port's screenshots are `D:\Recomps\lbp3-recompiled\analysis\screenshots\s37.png` and `freeze1.png`. They hold game imagery, so look at them but never commit or publish them.
2. **The Popit's panels.** Where do the Tools Bag and Gadgets panels sit on screen when they open? On the port they open mostly past the right edge of the screen (`s37.png`). Is that a port bug, or does the panel slide in from the edge the same way on a PS4?
3. **Touchpad and motion.** Does any part of story mode (any world, any of Sackboy, Oddsock, Toggle and Swoop) or Create mode need the DualShock 4 touchpad or motion sensing? Where, and is there a button alternative? The port maps the touchpad but reports a fixed orientation (upstream's `runtime/src/input/gamepad_sdl.cpp:233`, `:264-270`).

## Answer shape

`docs/research/ps4-reference.md` on the `research/ps4-reference` branch. For each question: the answer, the sources (URL plus timestamp for videos), and a verdict for the gap list: **port bug**, **expected behaviour** or **unknown**. Then a short `## Answer` here that gives the verdicts and links the file. Reading only: nothing is built or run.

## Answer

Videos are cited by YouTube's auto-generated stills at about 25%, 50% and 75% of the running time. No exact timestamps: the running times couldn't be read.

| Question | Verdict | Evidence |
|---|---|---|
| 1. Create-mode sky | **port bug** | On a PS4 a fresh level's default backdrop is pale cyan: five videos from three channels, two of them labelled PS4 ([Kaspar1111](https://www.youtube.com/watch?v=4PRAJwT1SHE) at about 25%; [SpottinGames [PS4 HD]](https://www.youtube.com/watch?v=1lJlDW_AvhA) at about 75%). The port's yellow-beige is that colour with red and blue swapped (sky R/G 1.03, B/G 0.71 on the port; 0.66 and 0.99 on PS4). The plank floor in the same frames matches the PS4 (R/G 0.72 on both), so only the sky pass is wrong. The backdrop is a default the creator can change, so "correct" means a fresh level nobody has touched, which is what the port's run used. |
| 2. Popit panels | **not a port bug** (corrected 2026-11-10, see Comments: the port screenshots were cropped) | On a PS4 an open bag page sits fully on screen at the right, from 72.8% to 97.6% of the width (Kaspar1111 at about 25%; SpottinGames at about 75%). On the port the Gadgets page rests with its left edge at 91.0%, about two-thirds off screen (`s33`–`s37`). The presenter shows the whole frame (`presenter.cpp:111`) and the safe area is 1.0 (`system_services.cpp:97-102`), so the shift is in the game's own layout. On a PS4 the page is at the same place with Sackboy at about 25% and about 50% of the width; nothing shows Sackboy further right (on the port he was at about 62%). |
| 3. Touchpad | **expected behaviour** | A Create-mode extra for drawing, painting and placing things ([PlayStation.com](https://www.playstation.com/en-us/games/littlebigplanet-3/); lead designer in [Outcyders](https://outcyders.net/article/little-big-planet-3-the-interview)), with a stick alternative (Prima, search excerpt). The same story and Create mode shipped on PS3, whose pad has no touchpad. No source ties it to the story. |
| 3. Motion | **unknown** | The documented uses are Sackboy's head and hip tilt and creator-wired Controlinator tilt. No source says any story level needs motion, and none rules it out. The full story run settles it. |

For the gap list: the touchpad half of gap 19 is settled; motion stays a check during the full story run. Side findings: Sony's stated target on PS4 is 30 fps at 1080p ([GamingBolt](https://gamingbolt.com/littlebigplanet-3-interview-sackboy-is-back-but-this-time-he-is-not-alone)), so running faster is a port bug (gap 3). The port's frames also seemed to have a 9-row black band along the top that the PS4 footage lacks. That was the capture crop too: the game draws a symmetric 14-row border at 1080 (9 at 720), and the crop hid the bottom one (see Comments).

Full findings: branch `research/ps4-reference`, file `docs/research/ps4-reference.md`.

## Comments

- 2026-11-10, **correction: verdict 2 was a screenshot artifact.**
  - **The cause:** `tools\capture.ps1` (and `screenshot.ps1`, and the rolling capture) ran DPI-unaware. At 125% Windows scaling the game window is 1280×720 physical pixels, but the scripts asked for its size unaware, got 1024×576, and `PrintWindow` filled only the window's top-left 80%, at true scale. Every port screenshot measured so far (`s33`–`s37` included) lacked the right and bottom fifth of the frame.
  - **With a DPI-aware capture** (patch [tools-capture-dpi.patch](../patches/tools-capture-dpi.patch)): the Tools Bag page's left edge sits at 72.7% of the width and the page is fully on screen (`analysis\screenshots\g7_dpifix_bag.png`). The PS4 reference is 72.8%. The old 91% was 930 px ÷ 1024 instead of 930 ÷ 1280.
  - **The same crop made the "top band":** the frame has a symmetric 9-row border at 720 (top and bottom), which is the game's own 14-row border at 1080 in its display buffer.
  - **Does a PS4 draw that border too? Probably.** Measured on the cited videos' 1280×720 stills (`i.ytimg.com/vi/<id>/maxres{1,2,3}.jpg`):
    - Kaspar1111 (all three stills): a black border on all four sides, 19–20 px left and right, 20 rows top and bottom. A safe-area ratio of about 0.97 gives the side border ((1 − 0.97) ÷ 2 × 1280 ≈ 19.5 px) and 11 of the rows; the game's 1.3% border at that ratio gives the other 9. Total 20, as measured.
    - SpottinGames [PS4 HD] (all three stills): no border on any side. With the game's border present on the console, that recording was most likely cropped or scaled on the way to YouTube; it can't separate the cases.
    - On the port, `LBP3_SAFE_AREA` gives the same kind of four-sided border (see above), so the port and the console agree on how the game draws it.
  - **Cross-check:** forcing the safe-area ratio (`LBP3_SAFE_AREA`, diagnostic build) to 0.9 and 0.95 moved the borders to exactly the values a 1280-wide crop predicts (64 px left and 44 top at 0.9, uncropped they are symmetric). The page moves left by about 3% of the width per 0.1 of ratio, as a safe-area inset should.
  - **What still stands:** the presenter does show the whole frame (`presenter.cpp:111`), as the Answer said.
  - **The original evidence has the same signature:** `s33`–`s37` are all 1024×576 with 9 black rows at the top and none at the bottom, while the window is created at 1280×720 (`presenter.cpp:24`). So they were cropped the same way.
