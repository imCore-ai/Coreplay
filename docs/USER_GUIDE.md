# Coreplay user guide

## What it plays

| Media | Engine |
|---|---|
| Every movie/audio container and codec FFmpeg reads (MOV, MP4, MKV, MXF, AVI, WebM, ProRes, DNxHR, H.264/H.265, AV1, VP9, ...) and URLs | libmpv, hardware decoding |
| Image sequences and single images: EXR (all compressions), TIFF, PNG, JPEG, DPX, Cineon, TGA, HDR, PSD, WebP, JPEG 2000, SGI, IFF | OpenImageIO, own RAM cache |

**Sequences.** Open any one frame. Coreplay finds the sequence (`shot.1001.exr` -> `shot.####.exr`),
its range and missing frames (light grey marks on the seek bar), and starts on the frame you opened.
Frames decode on all CPU cores into a RAM cache (grey on the seek bar; size in
Image > Sequence Cache Size). Playback shows every frame in order and waits for a frame
that is not decoded yet; it never skips. The status bar shows the real playback rate.

**Frame rate.** An EXR that carries `framesPerSecond` plays at that exact rational rate.
A file without it plays at the rate in Image > Sequence Frame Rate, and the status bar and
an on-screen message say that the rate did not come from the file.

**Colour.** Image sequences go through OpenColorIO on the GPU: input colour space ->
display / view (Image > Colour). `$OCIO` is used when set; else OCIO's built-in studio config
(it has every camera log space). Float files default to `scene_linear`, integer files to sRGB,
unless the file carries a colour tag the config knows. The default view is **Un-tone-mapped**
(the Nuke and RV default), so a PNG or JPEG shows exactly its own pixel values; the ACES views
are in Image > Colour, and the last choice is remembered. Channel isolation (R/G/B/A) and
exposure are in the Image menu.

## Viewer

- **Mouse wheel** zooms in and out around the point under the cursor; **Ctrl+wheel** jumps 20 s
  (as in MPC-HC). **Middle-drag** moves the picture.
- While zoomed, **1:1** (one image pixel per screen pixel) and **Fit** buttons appear at the top
  right of the picture. Also in View.
- **Double-click** the picture toggles full screen; a single click plays/pauses.
- **R / G / B** buttons in the control bar (and the R, G, B keys) show one channel as grey:
  one at a time, the same one again for full colour. The channel is taken from the picture as
  shown, after the colour view or LUT. Alpha is in Image > Channel.
- **Ctrl+Right / Ctrl+Left** rotate the picture 90 degrees clockwise / counter-clockwise.
- **Tab** shows the info overlay: file, resolution, frame rate, codec and, for EXR, the header
  (channels, compression, `framesPerSecond`, ...).
- **Full screen**: the seek bar and controls float over the picture, half translucent, and hide
  (with the cursor) 2 s after the mouse stops; moving the mouse brings them back.
- **Loop** (control bar): on by default. It cycles Loop Playlist, Loop This File and Off.

## A/B compare

Ctrl+Shift+A compares the playing clip (A) with the next playlist item (B) on one frame-locked
timeline. Modes: **A/B Flip** (F6), **Wipe** (F7, drag the line), **Side by Side** (F8) and
**Difference** (F9, shows only what changed). `\` swaps A and B; Alt+Shift+Right / Left moves B one
frame later / earlier. B can have its own LUT (Compare menu).

## Colour grading

Alt+G opens the grading window: a translucent window you can move, used while the clip plays.

- **Primary:** Brightness, Contrast, Saturation, Hue, and Gamma per channel (R, G, B).
- **Tone & Tint:** Shadows, Midtones and Highlights, each with its own weight and colour tint.
- One grade definition drives the viewer (movies and sequences), Export and saved stills, so all
  three give the same result. In compare mode the grade applies to A only.
- The clip is then marked as modified. **Save** writes the graded clip in place (the original
  goes to the Recycle Bin / Trash first, so it can be restored), **Save As** writes a new file,
  **Export** writes any format below. A sequence always goes to Save As, in another folder.

## LUTs

View > LUT, and the right-click menu > LUT: **Load a LUT...** (any OpenColorIO file format: .cube,
.3dl, .csp, .spi1d/.spi3d, .clf/.ctf, .cdl/.cc, ...), **LUT Presets** and **Clear LUT**.
Presets convert from Linear Rec.709, Linear Rec.2020, ACEScg, ACEScct, ARRI LogC3 / LogC4,
Sony S-Log3 (S-Gamut3, S-Gamut3.Cine, Venice), Blackmagic Film Gen5, DaVinci Intermediate,
RED Log3G10, Canon Log 2 / 3, Panasonic V-Log, DJI D-Log and Apple Log **to sRGB, Rec.709
(BT.1886) or Rec.2020 (BT.1886)**. They are colorimetric conversions without tone mapping
(the ACES tone-mapped views stay in Image > Colour). A LUT applies to the file's own values
and replaces the colour-space/view transform while it is on. The status bar shows it.

Both engines run the LUT on the GPU: image sequences in the OCIO shader, movies as a 65^3
3D texture in an mpv shader right after YUV->RGB, so hardware decoding stays zero-copy.

## Export

File > **Export...** (Ctrl+Shift+E, also on the right-click menu) converts the current clip.
It runs a second, windowless libmpv in encoding mode, so anything Coreplay plays can be
exported. Formats:

- **Video:** H.264 and H.265 (x264/x265, or the NVIDIA GPU encoders), AV1 (SVT-AV1, or
  NVIDIA RTX 40+), VP9 WebM, ProRes 422 Proxy / LT / 422 / HQ / 4444, DNxHR HQ / HQX / 444,
  CineForm, FFV1 MKV, Ut Video, QuickTime Animation, HAP, Motion JPEG, animated GIF.
- **Image sequences:** OpenEXR half / float, DPX 10 / 16-bit, TIFF 8 / 16-bit, PNG 8 / 16-bit,
  JPEG, WebP, JPEG XL - written as `name.####.ext` from a first frame number you choose.
- **Audio only** (from movies): WAV 24-bit, FLAC, MP3, AAC M4A, Opus.

Options: the whole clip or the A-B marks; **apply the LUT** (or, for a sequence, its colour
view) - off by default for OpenEXR so linear data stays linear; bake the grade; include audio
(ProRes / DNxHR / CineForm get 48 kHz PCM); **burn in subtitles** with the current subtitle style.

Timing is exact: image sequences keep their header rate, a missing frame is held (never
dropped, so the length stays right), and MP4/MOV files get their last frame's duration written.
An encoder this computer cannot use (for example NVENC without an NVIDIA driver) stops the
export at once with "Export failed".
Not available: MXF (use DNxHR in MOV).

## Subtitles

File > **Load Subtitle...** (Ctrl+L), or drop a subtitle file on a playing movie: .srt, .ass,
.ssa, .vtt, .sub, .smi, .lrc, .sup and more. Subtitles with the movie's name load by themselves.
**Subtitle Style...** (Play menu and right-click) changes the look live: font, size, bold,
italic, text colour (with transparency), outline width and colour, shadow offset and colour,
a background box with its colour and opacity in %, vertical position and margin, and whether
.ass/.ssa styles are overridden too. The style is remembered and is the one burned in by Export.
Subtitles show on movies, not on image sequences.

## Playlist

Ctrl+7 shows it. Drop files or folders on it (a folder adds its movies and sequences, and the
sequences one folder down). Drag rows to reorder; Delete removes rows; right-click for sort,
shuffle, save and load (`.m3u8`). A drop on the video opens (replaces the playlist);
Shift+drop appends. Playback between items is gapless.

## Hotkeys

Press **F1** for the full list in a translucent overlay. The default table is MPC-HC's, with
Coreplay's own changes for frame work: the arrows step frames, Shift+arrows jump 5 s, and
Page Up/Down go through the clips in the folder. `hotkeys.json` (View > Hotkeys, or press **O**)
holds only YOUR changes, for example `"play.playpause": ["Space", "K"]`; every command and its
default keys are in `hotkeys.defaults.json` next to it. The file reloads on save.

| Key | Action | Key | Action |
|---|---|---|---|
| Space | Play/Pause | . | Stop |
| Right / Left | Frame step / back | Shift+Right / Shift+Left | Jump 5 s |
| Wheel | Zoom at the cursor | Ctrl+wheel | Jump 20 s |
| Home | Jump to beginning | Ctrl+G | Go to time or frame |
| Ctrl+Up / Ctrl+Down / Ctrl+R | Rate x2 / /2 / reset | [ / ] | A-B mark A / B |
| Page Down / Page Up | Next / previous clip in the folder | Ctrl+PgDn / Ctrl+PgUp | Next / previous in the playlist |
| Up / Down | Volume | Ctrl+M | Mute |
| Num + / Num - | Audio delay +/-10 ms | A / Shift+A | Audio track |
| S / Shift+S / W | Subtitle track / on-off | F2 / Shift+F2 | Subtitle delay + / - |
| Alt+Enter, F11, double-click / Esc | Full screen / leave full screen | Click / middle-drag | Play/Pause / move picture |
| Alt+` / Alt+1 / Alt+2 / Alt+3 | Window 25 / 50 / 100 / 200 % | Alt+4 / Alt+5 | Auto fit / larger only |
| 1 / 2 / 3 | Minimal / compact / normal view | Ctrl+0 | Hide menu |
| Ctrl+1 / 2 / 3 / 4 / 5 / 7 | Seek bar / controls / info / stats / status / playlist | Tab | Info overlay |
| Ctrl+Right / Ctrl+Left | Rotate clockwise / counter-clockwise | Ctrl+A | Always on top |
| Ctrl+O / Ctrl+Q | Open / quick open | Ctrl+E / Ctrl+C | Reopen / close |
| Ctrl+S / Ctrl+Shift+S | Save / Save As | Ctrl+Shift+E | Export |
| Ctrl+Shift+A | Compare A/B | F6 / F7 / F8 / F9 | Flip / Wipe / Side by Side / Difference |
| Alt+G | Colour grading | Ctrl+L | Load subtitle |
| Alt+I / F5 | Save image / auto | Delete | Move file to Recycle Bin / Trash (asks first) |
| R / G / B | Red / green / blue channel (toggle) | F1 | Hotkey list |
| O | Settings | Alt+X | Exit |

Not carried over from MPC-HC (no meaning here): DVD/BD menus, capture/tuner, shader toggles,
subresync, favourites, VSync controls. Their keys are free.

## Language

Coreplay speaks 17 languages: English, 繁體中文, 简体中文, 日本語, 한국어, Español, Français,
Deutsch, Português, Русский, ไทย, العربية (right to left), हिन्दी, Bahasa Indonesia, Tiếng Việt,
Bahasa Melayu and Filipino. Settings (O) > General > **Language**: **Follow system** uses the
display language of the operating system (English when it is not one of these), or pick one
from the list. The new language applies when Coreplay starts again; **Restart Now** does
that for you. Product, format, codec, colour-space and key names stay in English.

## Opening files

`coreplay <files>` opens them; a second launch sends its files to the running window; `--new`
opens a second window. On Windows, files opened together from Explorer go into one playlist in
one window.
