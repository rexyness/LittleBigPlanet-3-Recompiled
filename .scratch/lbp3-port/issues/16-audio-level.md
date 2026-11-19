# Audio level: the game plays far too quietly on stereo speakers

Type: task
Status: in progress (fix built and tested offline 2026-11-12; in-game levels and the human's listen still to come)
Blocked by: none
Map: [LBP3 native port](../map.md) (done; reported by the human while playing the 1440p build on 2026-11-12: "the audio is hella low")

## Question

Why is the game so quiet on the human's stereo speakers, and what brings it to a normal level without clipping?

## Done when

- The cause is shown in code and by measurement, not guessed.
- The fix keeps the old path behind a switch, and the offline tests pass.
- In game, the [audio] line shows the level rise on the 8-channel port, with nothing clipping.
- Human listen: the game sounds normal at their usual Windows volume.

## Comments

- 2026-11-12 evening, **cause and fix, offline** (branch `fix/audio-downmix` `13e4038` from `port` `7d04e28`, pushed to the fork; nothing run in game yet, because the human was playing):
  - **Cause (code plus a measurement through the linked SDL):**
    - LBP3's FMOD mixes to an 8-channel float port (port 2 in the logs: `type 0, 1024 frames, 8 ch float`).
    - The human's device is stereo (`Speakers (Realtek(R) Audio) (48000 Hz, 2 ch)`).
    - `runtime/src/hle/audioout.cpp` created each port's SDL stream with the port's channel count and let SDL convert to the device.
    - SDL's `SDL_Convert71ToStereo` uses a normalised matrix whose coefficients add up to 1, so it can never clip: front pair 0.212, centre 0.150, sides 0.194/0.086, backs 0.181/0.111, LFE 0.067. Plain stereo content in the front pair therefore comes out at 0.212, about −13.5 dB, and the centre (dialogue) at 0.150, about −16.5 dB.
    - `audiotest` measured 0.2119 (−13.5 dB) through the SDL the runtime actually links (3.4.16 prebuilt; the matrix was read from shadPS4's vendored 3.5.0 source).
  - **The other ports are fine:** port 1 (2 ch s16) needs no conversion, and port 3 (mono float, type 2) is copied to both channels at unity by SDL's mono → stereo.
  - **No stereo query to answer:** in two full play logs the game never called an unimplemented `libSceAudioOut` function (no `[stub] libSceAudioOut` line), so there is no port-state or speaker query to answer "stereo" to. Answering stereo so that FMOD would mix stereo itself is not an option.
  - **The fix:**
    - On a stereo device, the runtime downmixes an 8-channel port itself (`runtime/src/hle/audio_downmix.{h,cpp}`) and hands SDL plain stereo: L = FL + 0.707·FC + 0.707·SL + 0.707·BL (R likewise), LFE dropped.
    - A peak limiter acts only when a buffer would go over −1 dBFS: a 1 ms attack ramp, a ~150 ms release, and a soft knee so the output never exceeds 1.0.
    - Per-port volume (`sceAudioOutSetVolume`) and `LBP3_VOLUME` still apply.
    - Devices with more than two channels still get SDL's conversion.
    - `LBP3_DOWNMIX=sdl` brings back SDL's matrix.
  - **What to expect, inferred from the weights:** front and centre content about +13.5 dB louder than before, surround content about +11 dB.
  - **Logging:**
    - An `[audio] levels over 5 s` line gives each port's RMS and peak in dBFS, before the port volume.
    - For the 8-channel port it also gives what SDL's and the runtime's downmix make of the same input, the output, and the limiter counts (buffers reduced, the lowest gain, soft-knee samples).
    - Port volume changes are logged (first 20 per port).
  - **Offline:**
    - New `audiotest`, 32 checks, 0 failed: the downmix weights and channel separation; SDL's matrix measured through a real SDL stream and the runtime's copy matching it to 1e-4; the limiter untouched at −6 dBFS, never over 1.0 at full scale on all 8 channels, held at −1 dBFS and back to unity after 1.5 s of quiet; on SDL's dummy device the 8-channel port reaches SDL as stereo, as 8 channels with `LBP3_DOWNMIX=sdl`, and the stereo and mono ports are untouched.
    - The seven existing test programs pass.
    - Limitation (inferred): SDL still sums the three ports in its mixer, so loud passages on several ports at once could still clip there. The [audio] line's peaks will show whether that happens.
  - **Test build for the human:**
    - Branch `exp/res1440-audio` `c1d74ff`, which is `exp/res1440` `928c42f` merged with `fix/audio-downmix`, pushed to the fork (shadPS4 unchanged at `56120048`).
    - Worktree `D:\Recomps\lbp3-wt\r1440a`, with the launcher `play_1440a.bat`: 1440p, frame generation, the audio fix, and logs in `analysis\play_1440a_log.txt`.
    - It builds, and all eight test programs pass in it.
  - **Next, when the game is free:** compare the [audio] lines in the Pod and the Introduction (music and voice) with the fix and with `LBP3_DOWNMIX=sdl`: the RMS rise, the peaks, the limiter counts. Then the human listens.

- 2026-11-12 evening, **the human listened** on `play_1440a.bat` (1440p + frame generation + the downmix): "voices are clear, sound is good. no crackles whatsoever". Their session's `[audio]` lines (`analysis\play_1440a_log.txt`) show the runtime's downmix louder than SDL's by the figures in the next comment, the peaks well under 0 dBFS, and the limiter not engaged.
- 2026-11-12 evening, the numbers from that session: across 52 five-second windows with sound, the downmix ran 11.8–13.5 dB louder than SDL's would have (median 13.5). The highest peak was −5.4 dBFS, and the limiter engaged in 0 buffers. That settles the in-game measurement without a separate test run. The fix is ready to merge with 1440p.
