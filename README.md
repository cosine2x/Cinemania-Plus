# Cinemania Plus

Cinemania Plus is a MediaTracker video export mod for TrackMania United Forever, based on [Cinemania](https://github.com/mediocre-q/Cinemania) by **mediocre-q**, with optional
**NVIDIA GPU (NVENC) video encoding** added by **cosine2x**. All the original work and credit belong to mediocre-q; this project is a modified version under the same GPLv3 license.

## Features

- MP4 export
- **GPU encoding with NVIDIA NVENC**, with automatic fallback to CPU (x264) encoding
- HD replays for smoother car motion

## Requirements

- Windows 64-bit
- TrackMania United Forever 2.12.0
- For GPU encoding: an NVIDIA GeForce GTX 10-series or newer and a recent NVIDIA driver (the bundled FFmpeg needs driver 610 or newer; with an older driver or another GPU, the CPU encoder is used automatically)
- [TMLoader 1.0.1 or newer](https://tomashu.dev/software/tmloader/)

## Install

1. Close TMLoader.

2. Open `%LOCALAPPDATA%\TMLoader\config.yaml` and add the following URL under `servers:`:

   ```yaml
   servers:
     - 'https://raw.githubusercontent.com/cosine2x/Cinemania-Plus/master/modloader/'
   ```

3. Reopen TMLoader and select **Cinemania Plus** in your profile. If you have the original Cinemania installed, do NOT enable both of them at the same time.

4. Export a video normally through MediaTracker.

## Record an HD replay

**Record HD Replay** captures the car's movement every 10 ms. The denser samples make the car move more smoothly in rendered footage, especially at high frame rates or in slow motion. The recording includes the full three-second braking outro.

1. Select a replay in the replay editor.
2. Select **Record HD Replay**. Cinemania replays the inputs at a high sample rate and saves `<original_name>_HD.Replay.Gbx` beside the source. If that HD copy already exists, Cinemania replaces it.
3. Select the new `_HD` replay and choose **Shoot video** to render it or edit it in the MediaTracker and render from there.

## GPU encoding

Cinemania Plus tries NVENC first. A short test encode runs before every export. If it fails (no NVIDIA GPU, old
driver), the original CPU encoder is used and the export works the same as in Cinemania. You can force a mode by setting the
environment variable `CINEMANIA_ENCODER` before starting TMLoader:

- `cpu` always uses the CPU (x264) encoder.
- `gpu` always uses NVENC and shows an error instead of falling back.
- not set (default) tries the GPU first.

## Credits and license

- Cinemania: mediocre-q, https://github.com/mediocre-q/Cinemania
- GPU encoding: cosine2x
- Licensed under the GNU General Public License version 3 (see `LICENSE`). Notices for bundled FFmpeg and MinHook
  are in `licenses/PLUS-NOTICE.md`.
