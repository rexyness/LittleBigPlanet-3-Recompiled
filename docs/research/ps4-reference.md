# How LBP3 looks and plays on a real PS4 where the port is in doubt

Research for [issue 08](../../.scratch/lbp3-port/issues/08-ps4-reference.md). Reading only: nothing was built or run.

**Sources and method.** Real-PS4 evidence comes from gameplay videos, official PlayStation pages and posts, interviews, reviews and wikis. I checked the videos through the stills YouTube generates for every upload. `https://i.ytimg.com/vi/<id>/hq1.jpg`, `hq2.jpg` and `hq3.jpg` are taken at about a quarter, a half and three-quarters of the running time. So a video citation below gives the share of the running time and links the still, which anyone can open to check. Exact minute:second timestamps weren't possible: YouTube's watch pages gave the fetch tool no running time or other metadata, and the browser extension that could have played the videos wasn't connected.

Colours are the mean R, G, B over a region of each still and of the port's screenshots. YouTube compression and capture setups change brightness, so the comparisons use ratios between channels, not raw levels. The port's screenshots (`D:\Recomps\lbp3-recompiled\analysis\screenshots\s33.png` to `s38.png` and `freeze1.png`, 1024×576 captures of the game window from the [baseline run](../../.scratch/lbp3-port/issues/02-baseline-run.md)) were looked at and measured only, never copied. Port code paths are in the local upstream clone, `D:\Recomps\lbp3-recompiled` at `ba83dcb` (upstream `dfc492a` plus the local SELF-unwrap commit).

Several sources couldn't be read directly. Prima's LBP3 guide, the Family Gaming Database, psthc.fr and Exophase answered 403, and the LittleBigPlanet wiki on Fandom answered 402. The Wayback Machine is blocked for the fetch tool. TCRF served the fetch tool a page of instructions addressed to AI agents instead of its content. Claims from those sites are marked *(search excerpt)*: they come from the search engine's summary of the page, not from reading it.

## Headlines

- **The sky is a port bug.** On a PS4, the default backdrop of a new level is a pale cyan sky. Five videos from three channels show it. The port shows the same backdrop in pale yellow-beige, which is the PS4 colour with red and blue swapped. The plank floor in the same frames has the PS4's hue, so the fault is confined to the sky.
- **The Popit panels are a port bug.** On a PS4, an open bag page (Tools, Gadgets, Materials) sits fully on screen at the right, from about 73% to 98% of the width. On the port the Gadgets page starts at 91%, so about two-thirds of it is past the right edge. In the PS4 footage the page is at the same place with Sackboy at about 25% and about 50% of the width.
- **The touchpad isn't needed (expected behaviour). Motion is unknown, probably not needed.** On PS4 the touchpad is an extra way to draw, paint and place things in Create mode. The PS3 version has the same story and Create mode on a pad without a touchpad. No source mentions the touchpad or motion in the story. The documented motion uses are Sackboy's head and hip tilt and a tilt input that creators can wire into their own levels.

## 1. The Create-mode sky

### What a PS4 shows

A fresh level opens in a room of blue-grey wooden planks: a plank floor, a wall of planks fanning out on the left, and a framed opening onto the sky. TCRF calls this default the "Blank Background" and says its floor uses repeating plank meshes *(search excerpt, [TCRF, LBP3 Popit items](https://tcrf.net/Proto:LittleBigPlanet_3_(PlayStation_3)/LBP3_r513946/Popit_Items))*.

| Video | Still | What it shows | Sky, mean RGB |
|---|---|---|---|
| Kaspar1111, [Create Mode Basics – Tools Bag Menu](https://www.youtube.com/watch?v=4PRAJwT1SHE) | about 25% ([hq1](https://i.ytimg.com/vi/4PRAJwT1SHE/hq1.jpg)) | The plank room, nothing built, Tools page open | 135, 204, 204 |
| Kaspar1111, [Create Mode Basics – Controls](https://www.youtube.com/watch?v=5casMpxG4io) | about 25% and 50% ([hq1](https://i.ytimg.com/vi/5casMpxG4io/hq1.jpg), [hq2](https://i.ytimg.com/vi/5casMpxG4io/hq2.jpg)) | Sackboy alone on the plank floor, flat sky across the top | 120, 182, 182 and 122, 185, 184 |
| AJGemau, [Create Mode – We Can't Build It? #1](https://www.youtube.com/watch?v=Hx8BiA4MREI) | about 25% ([hq1](https://i.ytimg.com/vi/Hx8BiA4MREI/hq1.jpg)) | The plank room with a car being built | 72, 133, 139 |
| SpottinGames, [Create Mode: Level 1 – Part 1 [PS4 HD]](https://www.youtube.com/watch?v=1lJlDW_AvhA) | about 75% ([hq3](https://i.ytimg.com/vi/1lJlDW_AvhA/hq3.jpg)) | A stone build in progress, sky behind it | 153, 215, 210 |
| SpottinGames, [Create Mode: Level 1 – Part 2 [PS4 HD]](https://www.youtube.com/watch?v=U1PMFwYm3VE) | about 25% ([hq1](https://i.ytimg.com/vi/U1PMFwYm3VE/hq1.jpg)) | The same build, sky between two stone walls | 141, 195, 194 |

Every sample is cyan: green and blue about equal, red well below them. Only SpottinGames names the platform (PS4). Kaspar1111 and AJGemau don't say. The two versions share their content (see [question 3](#3-touchpad-and-motion)), and the PS4-labelled footage shows the same sky.

The videos' upload dates couldn't be read, so they may predate 1.28. Patch 1.28 (October 2021) lists only fixes to Dive In and Challenges ([TheSixthAxis](https://www.thesixthaxis.com/2021/10/07/littlebigplanet-3-update-1-28-server-offline-downtime-maintenance/)). The 2016 update reworked the Pod's menus and added the Seaside Pier background with a level kit. It says nothing about a new level's default ([PlayStation Blog, 2016-08-15](https://blog.playstation.com/archive/2016/08/15/littlebigplanet-3-gets-major-ui-update-new-level-kit-and-mm-anniversary-pack/)). I found no source saying the default sky changed.

### What the port shows

The geometry, the planks and Sackboy match the PS4 footage. Only the sky differs:

- `s38`: Sackboy on the same plank floor under a flat pale yellow-beige sky.
- `s33`–`s37`: the plank room with the Popit open; the opening shows the same pale yellow.
- `freeze1`: the plank room with a Thruster firing; the sky is yellow-beige with light shafts across it.

| Frame | Sky, mean RGB | R/G | B/G |
|---|---|---|---|
| Port `s38` (x 200–900, y 60–300) | 202, 197, 139 | 1.03 | 0.71 |
| Port `s37` (x 520–900, y 50–400) | 211, 207, 148 | 1.02 | 0.71 |
| Port `freeze1` (x 250–450, y 40–200) | 168, 164, 112 | 1.02 | 0.68 |
| PS4, Controls video at about 50% | 122, 185, 184 | 0.66 | 0.99 |
| PS4, Tools Bag video at about 25% | 135, 204, 204 | 0.66 | 1.00 |

The port's ratios are the PS4's with red and blue exchanged. Swap red and blue in the port's `s38` sky and you get 139, 197, 202 (R/G 0.71, B/G 1.03), a pale cyan close to the PS4's. The floor in the same frames isn't swapped. The port's `s38` floor is 86, 119, 132 (R/G 0.72, B/G 1.11); the PS4 floor (Controls video, about 50%) is 54, 75, 86 (R/G 0.72, B/G 1.15).

So the port gets the sky alone wrong, and the pattern matches a red/blue channel swap in whatever draws or samples the sky. That points at a format or swizzle problem but doesn't prove it. It fits upstream's own guess of "a colour or format channel issue in the sky/backdrop pass" (`NOTES.md:663-664`); its "should probably be blue" is close, as the PS4 sky is pale cyan.

### Can the player change it?

Yes. A new level's backdrop is a default the creator can replace:

- The Tools Bag has a Backgrounds page *(search excerpt, [LittleBigPlanet wiki, Popit Menu](https://littlebigplanet.fandom.com/wiki/Popit_Menu?oldid=7302); the excerpt may describe an earlier game)*.
- Kaspar1111's Grids video builds on a different background, a child's bedroom ([about 25%](https://i.ytimg.com/vi/qKVUiX9jV84/hq1.jpg) of [the video](https://www.youtube.com/watch?v=qKVUiX9jV84)).
- Sony added backgrounds later, such as the Seaside Pier (PlayStation Blog 2016, above).
- TCRF describes a global visual option to hide the background that was cut before release *(search excerpt, [TCRF](https://new.tcrf.net/LittleBigPlanet_3_(PlayStation_3)))*.

So "correct" means the default of a fresh level that nobody has changed. That is what the scripted run made (My Moon → Create!, [issue 02](../../.scratch/lbp3-port/issues/02-baseline-run.md)).

### Verdict: **port bug**

On a PS4 the default sky is pale cyan. The port's sky is the same colour with red and blue swapped, and nothing else in the frame is. The fix belongs to the video core. A first step is to find the draw that writes the sky with upstream's F9 trace, which lists each texture's format and `dst_sel` (`NOTES.md:669`), and compare it with the plank floor's draw.

## 2. The Popit's panels

### What a PS4 shows

An open bag page stands fully on screen at the right, with a small margin:

| Video | Still | Page | Left edge | Right edge | Sackboy at |
|---|---|---|---|---|---|
| Kaspar1111, Tools Bag Menu | about 25% ([hq1](https://i.ytimg.com/vi/4PRAJwT1SHE/hq1.jpg)) | Tools | 72.8% | 97.6% | about 50% |
| Kaspar1111, Tools Bag Menu | about 50% ([hq2](https://i.ytimg.com/vi/4PRAJwT1SHE/hq2.jpg)) | Gadgets | 72.6% | 97.8% | about 50% |
| Kaspar1111, Tools Bag Menu | about 75% ([hq3](https://i.ytimg.com/vi/4PRAJwT1SHE/hq3.jpg)) | Choose Shape | same place | same place | about 50% |
| SpottinGames, Part 1 [PS4 HD] | about 75% ([hq3](https://i.ytimg.com/vi/1lJlDW_AvhA/hq3.jpg)) | Materials (Goodies Bag) | 72.7% | about 98% | about 25% |

The percentages are shares of the picture's width. Kaspar1111's picture has thin black borders and spans x 8–472 of the 480-pixel still. In the Gadgets and Choose Shape stills, two dimmed pages also hang to the left of the active one, mid-screen.

The Popit's own menu, the round or square one that comes before the bags, is different: it opens where Sackboy is. In SpottinGames' Part 1 at about 50% it's in the middle of the screen ([hq2](https://i.ytimg.com/vi/1lJlDW_AvhA/hq2.jpg)). In Part 2 at about 25% it hangs above Sackboy at about 44% of the width ([hq1](https://i.ytimg.com/vi/U1PMFwYm3VE/hq1.jpg)).

### What the port shows

In `s33`–`s37` (the scripted run), the Gadgets page's left edge is at x 932 of 1024, 91.0% of the width, and the screen edge cuts off the rest. Only the first one or two columns of icons and the start of each section heading show; the title reads "Gadg". If the page is as wide as on a PS4, about 25% of the width, then about two-thirds of it is off screen. That puts it about 18% of the width (about 350 pixels at 1920) right of where a PS4 shows it. The tooltips ("USE!", "More!" and the item's name) sit to the left of the highlighted icon and are fully readable, which is how the human could still pick a Thruster. Sackboy stands at about 62% of the width.

The page doesn't move across the five screenshots while the highlighted item changes (Mover, Force Chip, Advanced Mover, Follower, In/Out Mover). So this is where the page comes to rest, not a frame caught mid-slide. The layer gauge on the left is where a PS4 puts it: its left edge is at 4.9% of the width, against 3.5–3.9% in the PS4 stills. So the whole screen isn't offset or stretched; only the page is.

### Does the position depend on the cursor?

Not on a PS4, as far as the footage goes: the page sits at the same place with Sackboy at about 50% (Kaspar1111) and about 25% (SpottinGames). I found no footage of a page opened with Sackboy right of the middle, so a dependence there can't be ruled out. In the port's run Sackboy was at about 62%. A quick check on the port is to open the Tools Bag with Sackboy at the far left: if the page still starts at 91%, the cursor isn't the cause.

### Ruled out on the port side

- **The presenter and the screenshots.** The presenter draws the whole game frame onto the whole window, with no source or destination rectangle (`runtime/src/video/presenter.cpp:111`). The screenshot tool captures the window's whole client area (`tools/capture.ps1`). So the cut-off is in the game's own frame.
- **The safe area.** The display safe area is reported as 1.0 (`runtime/src/hle/system_services.cpp:97-102`), the same value shadPS4 returns (`third_party/shadPS4/src/core/libraries/system/systemservice.cpp:1727-1733`).

What's left is the game's own layout code getting a different answer on the port. Which input it reads differently isn't established.

### Verdict: **port bug**

On a PS4 the bag pages sit fully on screen at the right, at the same place for different Sackboy positions. On the port they come to rest about two-thirds off screen.

## 3. Touchpad and motion

### Touchpad

On PS4 the touchpad is a Create-mode extra:

- Sony's game page lists drawing shapes with the touchpad as a PS4 feature ([PlayStation.com](https://www.playstation.com/en-us/games/littlebigplanet-3/)).
- The PlayStation Blog announced it as "Touch Create" ([2014-08-12](https://blog.playstation.com/2014/08/12/littlebigplanet-3-new-create-tools-and-features-detailed/)) and at launch described creating with a finger swipe ([2014-11-24](https://blog.playstation.com/archive/2014/11/24/everything-need-know-littlebigplanet-3-week)).
- The lead designer, Jonathan Christian, said the PS4 has touchpad support as well, for painting stickers and drawing materials in Create mode ([Outcyders, 2014-11-21](https://outcyders.net/article/little-big-planet-3-the-interview)).
- Prima's guide says a sticker can be moved with the left stick or the touchpad *(search excerpt, [Prima](https://primagames.com/eguides/little-big-planet-3-signature-series-strategy-guide/welcome-back-to-littlebigplanet))*.
- Reviews tie the touchpad to building, for example to moving elements into place *(search excerpt, [Engadget](https://www.engadget.com/2014-11-18-littlebigplanet-3-review-knitting-lessons.html))*.

No source ties the touchpad to any story level, any of the four characters, or the Popit Puzzles.

The button alternative is built in. LBP3 came out on PS3 and PS4 together ([Wikipedia](https://en.wikipedia.org/wiki/LittleBigPlanet_3); [GamingBolt, 2014-07-23](https://gamingbolt.com/littlebigplanet-3-interview-sackboy-is-back-but-this-time-he-is-not-alone)), and the PS3's DualShock 3 has no touchpad. TCRF describes the PS4 version as a port of the PS3 game. Its additions are better character models and textures, level trailers and a gadget for live streams *(search excerpt, [TCRF, PS4 version](https://tcrf.net/LittleBigPlanet_3_(PlayStation_4)))*. So the story and Create mode can be done with buttons and sticks, and touch is an alternative on PS4.

What pressing the touchpad does in LBP3 is not in any source found (upstream maps Back to it for pads without one, `runtime/src/input/gamepad_sdl.cpp:46`).

### Motion

The DualShock 3 has motion sensing, so the PS3 argument doesn't settle motion. What the sources do show:

- **Head and hips.** In the series, tilting the pad shakes the Sackperson's head, and tilting with a button held wiggles its hips. That's play, not progress. The source is the first game's PS3 manual ([ManualZilla](https://manualzilla.com/doc/5983458/manual)), not LBP3's. A 2014 commenter on the PlayStation Blog asks to turn off LBP3's DualShock 4 tilt because it turns Sackboy's head *(search excerpt, [comment page](https://blog.playstation.com/2014/06/20/a-closer-look-at-littlebigplanet-3s-new-characters/comment-page-1/))*, so LBP3 kept it.
- **The Controlinator.** In LBP2's Create mode, the Controlinator can map controller tilt to a creation's controls *(search excerpt, [GamesRadar](https://www.gamesradar.com/littlebigplanet-2s-controlinator-could-be-the-best-platforming-invention-since-some-sort-of-sliced-food))*. LBP3 has the Controlinator too. A creator chooses to use tilt; the tools themselves don't need it.
- **PS Move and the accessibility listing.** The store lists PS Move as optional *(search excerpt, [PlayStation Store](https://store.playstation.com/en-us/product/UP9000-CUSA00473_00-LBP3GAME00000001))*, and the PS Move is out of scope ([map](../../.scratch/lbp3-port/map.md)). The Family Gaming Database lists motion gesture and motion pointing without saying what for *(search excerpt, [Family Gaming Database](https://www.familygamingdatabase.com/accessibility/LittleBigPlanet+3))*; that may be the PS Move.
- **The story.** No source mentions motion in the story, either way.

### Verdict: touchpad **expected behaviour**, motion **unknown**

- **Touchpad: expected behaviour.** Neither the story nor Create mode needs it. Its Create-mode uses have stick alternatives, and the port maps the touchpad anyway.
- **Motion: unknown, and the sources are thin.** The documented uses are Sackboy's head and hip tilt and creator-wired Controlinator tilt. With the orientation fixed (`gamepad_sdl.cpp:233`, `:264-270`), those may do nothing on the port. No source says a story level depends on motion, but none says it doesn't. The full story run will settle it.

## Bearing on other gaps

- **Frame rate (gap 3, [issue 06](../../.scratch/lbp3-port/issues/06-frame-pacing.md)).** Sony said LBP3 targets 30 fps at 1080p on PS4 so it could be built for both consoles. That corrected the producer's earlier yes to 60 fps ([GamingBolt](https://gamingbolt.com/littlebigplanet-3-interview-sackboy-is-back-but-this-time-he-is-not-alone)). So the port's 30 fps cap is right, and running faster is a port bug. No measured frame-rate analysis of the PS4 game turned up (a search for Digital Foundry's found none).
- **The Legend of Bunkum (gap 8).** In a recording of the cutscene at about 75%, the action is still going: Newton and Sackboy outdoors in daylight, with subtitles ([hyperdude95, Prologue – The Legend of Bunkum](https://www.youtube.com/watch?v=zBMwCD_dhC8), [hq3](https://i.ytimg.com/vi/zBMwCD_dhC8/hq3.jpg)). The platform isn't stated and the running time couldn't be read, so this can't be matched to the port's white screen at about 106 s. Thin.
- **A band along the top of the port's frames.** `s33`–`s38` and `freeze1` all have 9 black rows at the top (1.6% of the height, about 17 rows at 1080) and none at the bottom. The PS4 stills have no such band. I didn't check whether it comes from the capture or from the game's output.
- **Panel colour, not a gap.** The port's Gadgets page is blue (84, 149, 185). In Kaspar1111's footage the Tools Bag pages are red to purple. But the Goodies Bag's Materials page in SpottinGames' PS4 footage is the same blue (86, 147, 165). So the port's panel colour is a real PS4 panel colour, not a second channel swap. The difference is probably the version or the page.

## Where the sources are thin

- No video gave an exact timestamp. The stills are at about 25%, 50% and 75% of each video.
- Only SpottinGames labels its platform. Kaspar1111's and AJGemau's videos show the same sky and layout but don't name the platform.
- The footage may predate 1.28. The patch notes found don't touch Create mode.
- Motion in the story: no direct source either way.
- LBP3's own manual wasn't found. The only manual found is the first game's, for PS3.
