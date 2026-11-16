# Debug overlay

A development overlay for tuning the look and feel of 3D scenes on the top
screen (`platform/3ds/debug_overlay.c`). It is drawn by the stage renderer's
development HUD (libctru 8x8 font, a translucent box in the top-left corner)
and is off unless turned on.

- **Toggle:** hold L and R and press START. The START press is consumed, so
  the field's menu does not open.
- **On from boot:** create an empty file `sdmc:/3ds/Colosseum3DS/debug-overlay`.

| Line | Shows |
| --- | --- |
| 1 | scene, frames per second and frame time (smoothed over about 10 frames) |
| 2 | CPU draw preparation, last completed GPU frame, time spent waiting (VBlank/GPU) in ms |
| 3 | draw calls, submitted vertices, CPU-prepared vertices |
| 4 | camera eye |
| 5 | camera target and eye-target distance |
| 6 | field of view, zoom against the scene's reference field of view (when it has one) and roll |
| 7-8 | scene lines |

Scene lines:

- **field:** floor and Wes's position; the orbit camera's zoom, yaw and
  pitch offsets (D-pad Up/Down and the C-stick change them).
- **worldmap** (the orbit preview): the previewed floor and orbit radius;
  zoom is against the preview's 40-degree field of view.
- **journey:** the `lbl_3_data_270` row, route animation, camera resource and
  bike; ride time, fade and the destination prefetch (`loaded/total`).

The numbers describe the previous submitted frame. Emulator frame rates
(Azahar is host-bound) are not hardware performance.
