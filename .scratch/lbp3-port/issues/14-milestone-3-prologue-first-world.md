# Milestone 3, the Prologue and the first world

> **Moved to GitHub:** https://github.com/rexyness/LittleBigPlanet-3-Recompiled/issues/17 (#17) on 2026-11-19. This file is the archive as of the move; work on the issue.

Type: task
Status: moved to #17 (2026-11-19); was: in progress (the human has played through Titan Released; local co-op, ticket 13, closes with this milestone)
Blocked by: none
Map: [LBP3 native port](../map.md) (done; this is the third build ticket from the [spec](../spec.md))

## Question

Play the Prologue and the first world on `port` and fix what breaks, as the [spec](../spec.md#milestone-ladder) says: gap 9 (recheck the Needlepoint launch), gap 16 begins (the story past Needlepoint Peaks), gap 19 (watch for motion and touch-pad needs), gap 20 (local co-op, [13](13-local-co-op.md)).

## Done when

- The human plays the Introduction → Needlepoint Peaks → the first hub and its levels with no freeze, no cutscene skip and nothing the game can't get past; Sackboy isn't launched out of Needlepoint Peaks.
- After the world the agent keeps a save snapshot (`analysis/savedata_backups/`), so later checks rerun from there.
- Local co-op works with the human and a second player (ticket 13).
- Anything that breaks has its own ticket and fix.

## Comments

- 2026-11-11, **the human's play so far** (logs archived in the clone's `analysis/play_logs_20261111/`; per-level numbers from `tools/perlevel.py`, which attributes each `[video]` status window, about 30 frames, to the last level archive opened):
  - 13:30 session: the story to Stitchem Manor (ticket 12's comments). 14:11 session: an hour of play; its log rotated away before it was archived (only its last-minute numbers survive: 45–60 ms frames, game work 45–48 ms, in a heavy scene).
  - 19:08–20:09 session (`play_log.1.txt`): Needlepoint Peaks, Stitchem Manor, Stitchem Gate, Stitchem Boss, Newton's Airship and Titan Released. No GPU warning, device loss, stuck wait or crash; closed normally. Snapshot `analysis/savedata_backups/human_play_20261111_2009`.
  - 20:27–21:03 session (co-op build, alone): the Manglewood hub and the Diner. Snapshot `human_play_20261111_2104`.
  - **Gap 9:** no launch out of Needlepoint Peaks in two sessions there.
  - **Gap 19:** nothing so far needed motion or the touch pad.
  - **Frame time per level** (average frame, average game work, frames over 40 ms):

    | Level | Avg | Work | Over 40 ms |
    |---|---|---|---|
    | Needlepoint Peaks | 37.7 ms | 33.9 ms | ~23% |
    | Stitchem Gate | 38.2 ms | 34.4 ms | ~30% |
    | Stitchem Manor | 34.5 ms | 31.8 ms | ~20% |
    | Stitchem Boss | 33.7 ms | 28.6 ms | ~1% |
    | Newton's Airship | 33.5 ms | 28.9 ms | ~0.5% |
    | Titan Released | 33.7 ms | 24.9 ms | ~0.4% |
    | Manglewood hub | 34.1 ms | 25.8 ms | ~1% |
    | The Diner | 33.6 ms | 22.3 ms | ~0.4% |

    The heaviest levels run at or over the 33.3 ms budget, so frames slip to the next refresh. Single stalls of 0.5–1.4 s at level entry and occasionally mid-level (Titan Released 1.1 s, the Diner 1.4 s, Manglewood 1.1 s). Both are milestone 5's (gap 10); a read-only audit of the runtime's per-frame costs is in [perf/audit.md](../perf/audit.md).
  - Still to play: the rest of the first world.
