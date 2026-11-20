# The upscaler works at the end of the frame, on the HUD-less image

NVIDIA's DLSS programming guide puts the upscaler before tone mapping, as close to the start of post-processing as possible, with the UI drawn afterwards at the output size. The game's frame doesn't divide that way: the 3D scene, then depth of field and motion blur, SMAA, bloom, the in-world UI, the final composite with its colour grading, and last the screen HUD, all at the render size (`.scratch/lbp3-port/perf/fps60_render_re.md`). Putting the upscaler where NVIDIA says would mean running everything after it at a second size: a new reverse-engineering and patch job in the eboot at least as big as the render-size work. It wouldn't give the upscaler high-dynamic-range input either, because the game's scene buffer is already 8-bit before the composite.

So the upscaler takes the HUD-less image (the display buffer right after the final composite, before the screen HUD), the game's depth and its motion vectors (made 16-bit in the video core), with the game's SMAA skipped and the scene's draws jittered by the video core. The screen HUD is then put back on top from the finished frame wherever that frame differs from its HUD-less image, as frame generation already does, and frame generation runs on the upscaled pictures.

## Considered Options

- **Before post-processing, as NVIDIA's guide says.** Rejected for the first version on cost; reconsider only if the end-of-frame result disappoints in the side-by-side screenshots against native rendering.

## Consequences

- The upscaler sees motion blur, depth of field and colour grading already applied.
- The screen HUD is drawn at the render size and stretched, so its text is softer in the smaller upscaler modes.
- The in-world UI (the popit, the cursor, labels) goes through the upscaler without motion vectors of its own; a fix is added only if it visibly smears.
- The jitter and the skipped SMAA change the game's frame for everything that reads it, not only the upscaler:
  - the screen HUD is put back from the stretched final frame, so translucent HUD parts carry the jittered, un-antialiased scene behind them;
  - the game's own read-backs of its frame (the pause menu's fade, the Pod computer, photos and level icons) see that same scene.

