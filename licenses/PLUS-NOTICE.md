# Cinemania Plus notice

Cinemania Plus is a modified version of **Cinemania** by mediocre-q
(https://github.com/mediocre-q/Cinemania), which is licensed under the GNU
General Public License version 3. Cinemania Plus is distributed under the same
license; see `LICENSE`. The original copyright and credit belong to its author.

Changes in Cinemania Plus: the encoder worker (`src/worker/worker_session.cpp`)
can encode with NVIDIA NVENC (`h264_nvenc`) and falls back to the original x264
CPU encoder when NVENC is unavailable. Source:
https://github.com/cosine2x/Cinemania-Plus

## Bundled FFmpeg

`bin/ffmpeg.exe` is an unmodified GPL build of FFmpeg from
https://github.com/BtbN/FFmpeg-Builds (branch 9.0, win64-gpl, static). It is
not the minimal FFmpeg that the original Cinemania ships. FFmpeg and the
libraries linked into this build (including x264) are licensed under the GPL
(version 3 for this build); the license text of the build is included as
`licenses/FFmpeg-LICENSE.txt`.

The release page lists the exact FFmpeg version string and SHA-256 of the
bundled file in `BUILD-INFO.txt`. The FFmpeg commit in that version string
(for example `gd59078794d` means commit `d59078794d`) identifies the exact
FFmpeg source; build scripts and the full list of third-party sources are at
https://github.com/BtbN/FFmpeg-Builds. To obtain the corresponding source for
any bundled component, open an issue on the Cinemania Plus repository.

## MinHook

MinHook is included in the DLL under its own license, in
`licenses/MinHook-LICENSE.txt`.
