<p align="center">
  <img src="res/coreplay_wordmark.png" alt="Coreplay" width="360">
</p>

<h3 align="center">Every frame. Exactly as it is.</h3>

<p align="center">
  A small, fast player for movies and image sequences, made for people who work on films.<br>
  <b>A pro tool, free forever. For everyone. No sign-up. No ads.</b>
</p>

<p align="center">
  <a href="https://github.com/imCore-ai/coreplay/releases/latest/download/Coreplay-Windows-Setup.exe"><img src="docs/images/button-windows.svg" alt="Download for Windows 10 | 11" height="38"></a>
  <a href="https://github.com/imCore-ai/coreplay/releases/latest/download/Coreplay-macOS-arm64.dmg"><img src="docs/images/button-macos-arm64.svg" alt="Download for macOS 12+ on Apple silicon" height="38"></a>
  <a href="https://github.com/imCore-ai/coreplay/releases/latest/download/Coreplay-macOS-x86_64.dmg"><img src="docs/images/button-macos-intel.svg" alt="Download for macOS 12+ on Intel" height="38"></a>
  <a href="https://github.com/imCore-ai/coreplay/releases/latest/download/Coreplay-Linux-x86_64.AppImage"><img src="docs/images/button-linux-appimage.svg" alt="Download for Linux x86-64, AppImage" height="38"></a>
  <a href="https://github.com/imCore-ai/coreplay/releases/latest/download/Coreplay-Linux-amd64.deb"><img src="docs/images/button-linux-deb.svg" alt="Download for Linux x86-64, .deb package" height="38"></a>
  <br>
  <a href="https://github.com/imCore-ai/coreplay/releases/latest"><img src="https://img.shields.io/github/v/release/imCore-ai/coreplay?style=flat-square&label=version" alt="Latest version"></a>
  <a href="https://github.com/imCore-ai/coreplay/releases"><img src="https://img.shields.io/github/downloads/imCore-ai/coreplay/total?style=flat-square&label=downloads" alt="Downloads"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/source-Apache_2.0-3B7DDD?style=flat-square" alt="Source code: Apache 2.0"></a>
  <a href="https://github.com/imCore-ai/coreplay/stargazers"><img src="https://img.shields.io/github/stars/imCore-ai/coreplay?style=flat-square" alt="GitHub stars"></a>
</p>

<p align="center">
  Windows 10 and 11 · macOS 12 and later · Linux x86-64 · <a href="https://github.com/imCore-ai/coreplay/releases">All releases</a>
</p>

> [!IMPORTANT]
> **Code signing is in progress. The warning you may see is normal, and Coreplay is safe to run.**
> Coreplay is new, and its certificates from Microsoft and Apple are not issued yet. Until they are,
> Windows and macOS show a warning the first time you open it. That warning appears for every new
> app that is not signed yet. It does not mean that something is wrong with Coreplay: its full source
> code is in this repository, and every download is built from it by GitHub Actions.
>
> - **Windows:** on *Windows protected your PC*, click **More info**, then **Run anyway**.
> - **macOS:** open Coreplay once and click **Done**. Then open **System Settings → Privacy & Security**,
>   scroll down and click **Open Anyway**. Or run this one line in Terminal:
>   `xattr -dr com.apple.quarantine /Applications/Coreplay.app`
> - **Linux:** no warning.
>
> You do this only once. When the signing is done, the warning goes away.

![Coreplay: playing, an A/B wipe against the graded shot, and the grading window](docs/images/demo.webp)

<p align="center"><sub>Background: <i>God Being</i>, made with <a href="https://imcore.ai/core-studio.html">Core Studio</a>.</sub></p>

## Movies and frames, one window.

![Coreplay playing Warrior, made with Core Studio](docs/images/main.png)

<p align="center"><sub>Shown: <i>Warrior</i>, made with <a href="https://imcore.ai/core-studio.html">Core Studio</a>.</sub></p>

Open any movie, or any one frame of a sequence. Coreplay finds the rest of the shot, its
range and its missing frames, and plays it at its real frame rate, in the right colour.

| **0.25 s** | **0.5 s** | **0.2 s** | **0** |
|:---:|:---:|:---:|:---:|
| to paint the window | to the first movie frame | to the first EXR frame | skipped frames: playback waits, it never drops one |

## What it does

Everything you open in a day, in one fast player.

**01 · It plays everything.** Every movie and audio format FFmpeg reads, with hardware
decoding. And image sequences and single images, decoded on all CPU cores into a RAM cache.
<br><sub>MOV · MP4 · MKV · MXF · ProRes · DNxHR · H.264 / H.265 · AV1 · VP9 · EXR · DPX · Cineon · TIFF · PNG · HDR · PSD · JPEG 2000</sub>

**02 · Sequences, found for you.** Open one frame. Coreplay finds the sequence
(`shot.1001.exr` → `shot.####.exr`), its range and its missing frames, and starts on the
frame you opened. The seek bar shows what is cached and what is missing.

**03 · The real frame rate.** An EXR that carries its frame rate plays at that exact rate,
23.976 included. When a file does not carry one, Coreplay tells you so on screen. It never
guesses quietly.

**04 · Colour, managed.** Image sequences go through OpenColorIO on the GPU, with every
camera log space. The default view is un-tone-mapped, as in Nuke and RV, so a PNG shows
exactly its own pixel values. The ACES views are one click away.

**05 · LUTs and camera presets.** Load any LUT OpenColorIO reads, or pick a preset from a
camera log to sRGB, Rec.709 or Rec.2020. On movies and sequences alike, on the GPU, with
hardware decoding still on.
<br><sub>ARRI LogC3 / LogC4 · Sony S-Log3 · RED Log3G10 · Canon Log 2 / 3 · Panasonic V-Log · Blackmagic Gen5 · DaVinci Intermediate · Apple Log · DJI D-Log · ACEScg / ACEScct</sub>

**06 · Compare A and B.** Two clips on one frame-locked timeline: flip, wipe, side by side,
or a difference view that shows only what changed.

![A/B compare, Wipe mode: the graded shot (A, left) against the original (B, right)](docs/images/compare.png)

**07 · Grade while it plays.** Alt+G opens a translucent grading window over the picture:
brightness, contrast, saturation, hue, gamma, and a tint for shadows, midtones and
highlights. Save, Save As and Export bake the grade into the new file.

**08 · Export anything it plays.** Convert the clip, or just the A-B section, to a movie,
an image sequence or audio. Frame counts, frame rates and durations stay exact; a missing
frame is held, never dropped.
<br><sub>ProRes 422 / 4444 · DNxHR · H.264 / H.265 · AV1 · OpenEXR half / float · DPX 10 / 16-bit · TIFF · PNG 16-bit · WAV / FLAC</sub>

**09 · A viewer for checking work.** Zoom with the wheel around the cursor, pan with the
middle button, snap to 1:1 or Fit. Show the red, green or blue channel alone. Rotate the
picture. Step frame by frame with the arrow keys.

**10 · Subtitles and playlists.** Drop a subtitle file on a movie, style it live, and burn
it in on export. Drop files and folders on the playlist; folders add their movies and
sequences.

## Your hands already know it.

The keys are MPC-HC's, with changes for frame work. Press **F1** to see them all over the
picture, and change any of them in one small file: Coreplay reloads it when you save.

| Key | Action | Key | Action |
|---|---|---|---|
| Space | Play / pause | ← → | One frame back / forward |
| Shift ← → | Jump 5 seconds | [ ] | Mark A / B |
| PgUp PgDn | Previous / next clip in the folder | R G B | Red, green or blue channel |
| Ctrl G | Go to a time or a frame | Ctrl Shift A | Compare A / B |
| Alt G | Colour grading | Ctrl Shift E | Export |
| Alt Enter | Full screen | F1 | Every hotkey |

![Press F1 for every hotkey, over the picture](docs/images/hotkeys.png)

## Free. For everyone.

A pro tool, free forever. No sign-up, no ads, and it works fully offline. On Windows it
checks for updates by itself, and installs only an update signed by imCore.

Get it from the **[latest release](https://github.com/imCore-ai/coreplay/releases/latest)**:

| System | File | Install |
|---|---|---|
| Windows 10 / 11 | `Coreplay-Windows-Setup.exe` | Run it. Installs for your user only, no admin prompt. The first time, allow it as shown in the note at the top. |
| macOS 12 or later, Apple silicon (M1 and newer) | `Coreplay-macOS-arm64.dmg` | Drag Coreplay to Applications. The first time, allow it as shown in the note at the top. |
| macOS 12 or later, Intel | `Coreplay-macOS-x86_64.dmg` | The same. Not sure which Mac you have? Apple menu > About This Mac: "Chip" means Apple silicon, "Processor" means Intel. |
| Linux (x86-64) | `Coreplay-Linux-x86_64.AppImage` | `chmod +x` it and run it. Ubuntu 22.04+ / Debian 12+: or `sudo apt install ./Coreplay-Linux-amd64.deb`. |

More: [user guide](docs/USER_GUIDE.md) · [build from source](docs/DEVELOPMENT.md)

---

Made by [imCore](https://imcore.ai), the team behind
[**Core Studio**](https://imcore.ai/core-studio.html), a complete film studio, anywhere you are.

<sub>The source code is licensed under the [Apache License 2.0](LICENSE). The ready-made
downloads also contain libmpv and FFmpeg (GPL builds, with x264 and x265), Qt (LGPL), and
OpenImageIO, OpenEXR and OpenColorIO (BSD / Apache); because of the GPL parts, the downloads
as a whole are distributed under the GNU GPL v3.0.</sub>
