<p align="center">
  <img src="res/coreplay_wordmark.png" alt="Coreplay" width="360">
</p>

<p align="center">
  <b>A fast media player for film and VFX review.</b><br>
  Movies and image sequences (EXR, DPX, TIFF, PNG) in one window, with correct colour,
  exact frame rates and MPC-HC hotkeys. Windows, macOS and Linux. Fully offline.
</p>

<h3 align="center">Download</h3>

<p align="center">
  <a href="https://github.com/imCore-ai/coreplay/releases/latest/download/Coreplay-Windows-Setup.exe"><img src="https://img.shields.io/badge/Download_for-Windows-0078D6?style=for-the-badge" alt="Download for Windows" height="44"></a>
  &nbsp;
  <a href="https://github.com/imCore-ai/coreplay/releases/latest/download/Coreplay-macOS-arm64.dmg"><img src="https://img.shields.io/badge/Download_for-macOS-555555?style=for-the-badge" alt="Download for macOS" height="44"></a>
  &nbsp;
  <a href="https://github.com/imCore-ai/coreplay/releases/latest/download/Coreplay-Linux-x86_64.AppImage"><img src="https://img.shields.io/badge/Download_for-Linux-E95420?style=for-the-badge" alt="Download for Linux" height="44"></a>
</p>

<p align="center">
  <b><a href="https://github.com/imCore-ai/coreplay/releases/latest/download/Coreplay-Windows-Setup.exe">Windows installer</a></b> ·
  <b><a href="https://github.com/imCore-ai/coreplay/releases/latest/download/Coreplay-macOS-arm64.dmg">macOS .dmg</a></b> ·
  <b><a href="https://github.com/imCore-ai/coreplay/releases/latest/download/Coreplay-Linux-x86_64.AppImage">Linux AppImage</a></b>
</p>

<p align="center">
  <sub>Windows 10 / 11 · macOS on Apple silicon (M1 and newer) · Linux x86-64 AppImage
  (<a href="https://github.com/imCore-ai/coreplay/releases/latest/download/Coreplay-Linux-amd64.deb">.deb for Ubuntu 26.04+</a>)
  · <a href="../../releases/latest">all files and release notes</a></sub>
</p>

<p align="center">
  <a href="docs/USER_GUIDE.md">User guide</a> ·
  <a href="docs/DEVELOPMENT.md">Build from source</a>
</p>

![Coreplay playing GodBeing, a film made with Core Studio](docs/images/main.png)

## Installing

| Platform | How |
|---|---|
| **Windows** | Run `Coreplay-Windows-Setup.exe`. Per-user install, no admin rights; Coreplay updates itself. If SmartScreen warns, choose More info > Run anyway. |
| **macOS** | Open `Coreplay-macOS-arm64.dmg` and drag Coreplay to Applications. The first time, right-click Coreplay and choose Open. |
| **Linux** | `chmod +x Coreplay-Linux-x86_64.AppImage`, then run it. Runs on Rocky / Alma 9, Ubuntu 22.04+, Debian 12+, Fedora. On Ubuntu 26.04+ you can also `sudo apt install ./Coreplay-Linux-amd64.deb`. |

## Why Coreplay

- **One player for movies and frames.** Every format FFmpeg reads (MOV, MP4, MKV, MXF;
  ProRes, DNxHR, H.264, H.265, AV1, VP9), and image sequences: open any one frame and
  Coreplay finds the whole sequence, its range and its missing frames.
- **It never drops a frame.** Sequences decode on all CPU cores into a RAM cache.
  Playback shows every frame in order, at the exact rational rate in the file
  (for example 24000/1001), and says so when the file has no rate.
- **Correct colour.** OpenColorIO on the GPU, with every camera log space built in.
  LUT presets for ARRI LogC3/C4, S-Log3, RED Log3G10, Canon Log, V-Log, D-Log, Apple Log
  and ACES to sRGB, Rec.709 or Rec.2020, or load any `.cube` / `.3dl` / `.clf` file.
- **Review tools.** A/B compare (flip, wipe, side by side, difference) on one
  frame-locked timeline, zoom to 1:1, R / G / B channels, and an info overlay with the
  EXR header.
- **Live colour grading** (Alt+G) while the clip plays. Save, Save As and Export bake
  the grade into new files.
- **Export to delivery formats.** ProRes, DNxHR, H.264 / H.265 (also on NVIDIA GPUs),
  AV1, EXR, DPX, TIFF and PNG sequences, with exact frame counts and frame rates.
- **Fast.** The window paints in about 0.25 s; the first movie is ready in about 0.5 s.
- **Keyboard first.** The MPC-HC key table, changed for frame work. Press **F1** for the
  full list; change any key in `hotkeys.json`.

![A/B compare, Wipe mode: the graded shot (A, left) against the original (B, right)](docs/images/compare.png)

![Press F1 for every hotkey, over the picture](docs/images/hotkeys.png)

## Fully offline

Playback, grading and export need no network. The only network use is the update
check on Windows, which you can turn off in Settings. A Windows update installs only
when its signature and SHA-256 match.

## Build from source

See [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md): Windows (Visual Studio + Qt + vcpkg),
Linux .deb and AppImage (Docker), and the end-to-end tests.

## Licence

The Coreplay source code is licensed under the [Apache License 2.0](LICENSE).

The ready-made downloads on the Releases page also contain third-party libraries under
their own licences: libmpv and FFmpeg (GPL builds, with x264 and x265), Qt (LGPL),
OpenImageIO, OpenEXR and OpenColorIO (BSD / Apache). Because of the GPL parts, the
downloads as a whole are distributed under the GNU General Public License v3.0. To make a
build without GPL parts, build Coreplay against LGPL builds of libmpv and FFmpeg
(see [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md)).
