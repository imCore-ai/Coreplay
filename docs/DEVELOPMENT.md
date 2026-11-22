# Building and testing Coreplay

Coreplay is C++20 with Qt 6 (Widgets + OpenGL 4.1 core), libmpv for movies, OpenImageIO +
OpenEXR for image sequences and OpenColorIO for colour. The code is portable; Windows and
Linux are built and tested today.

## Windows

Needs Visual Studio 2022 Build Tools (C++), Qt 6.5+ (MSVC x64), vcpkg, and a libmpv SDK in
`third_party\mpv` (`include\mpv\*.h`, `libmpv-2.dll`, `mpv.lib`). `third_party\` is not in git
(libmpv is ~120 MB).

1. **Qt**: `python -m pip install aqtinstall`, then
   `python -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O C:\Qt`
   (run it from a folder you can write to: aqt writes its log to the current folder).
2. **vcpkg** (OpenImageIO, OpenColorIO from `vcpkg.json`):
   `git clone https://github.com/microsoft/vcpkg C:\vcpkg` and
   `C:\vcpkg\bootstrap-vcpkg.bat -disableMetrics`. The build installs the packages.
3. **libmpv**: `mpv-dev-x86_64-<date>-git-<hash>.7z` from
   https://github.com/shinchiro/mpv-winbuild-cmake/releases, unpacked into `third_party\mpv`
   with Windows' own `tar.exe -xf` (it reads .7z). It ships `libmpv.dll.a` (MinGW); make the
   MSVC import library from the DLL's exports, in a VS x64 prompt:

   ```powershell
   cd third_party\mpv
   dumpbin /exports libmpv-2.dll > exports.txt
   # write mpv.def: "LIBRARY libmpv-2", "EXPORTS", then one exported name per line
   lib /def:mpv.def /machine:x64 /out:mpv.lib
   ```

```powershell
.\build.ps1 -QtDir C:\Qt\6.8.3\msvc2022_64 -Vcpkg C:\vcpkg
```

Add `-Tests` for the test build (end-to-end driver + `mkexr`). Output: `build\coreplay.exe` with
its DLLs (the folder is self-contained; copy it anywhere).

### Installer

`.\build.ps1 -Installer` builds the release, stages a self-contained `dist\Coreplay` (app, Qt
plugins, libmpv, OpenImageIO, OpenColorIO, the C++ runtime) and compiles
`dist\Coreplay-<version>-Setup.exe` with NSIS (`installer\coreplay.nsi`).

- Per-user install, no admin prompt: `%LOCALAPPDATA%\Programs\Coreplay`, Start-menu shortcut,
  optional desktop shortcut, "Apps & features" entry. `/S` installs silently.
- File types (`installer\make_filetypes.py`): every type gets Coreplay under "Open with" and the
  right-click entries "Play with Coreplay" and "Add to Coreplay playlist". Ticked groups also
  make Coreplay the default (on: video, EXR/DPX/Cineon/HDR, playlists; off: audio, common
  images). Windows keeps any default the user chose earlier.
- Uninstall removes only Coreplay's own values, restores each type's previous default, and
  deletes only keys the installer created that are still empty.

### Updates

Help > **Check for Updates...** reads a signed feed (default
`https://auth.imcore.ai/coreplay-updates/windows/`; `COREPLAY_UPDATE_URL` points a copy at
another feed): `latest.json` (version, file, size, SHA-256, notes) and `latest.json.sig`.
Coreplay trusts the manifest only when the signature verifies against the public key built into
it (`src/UpdateKey.h`, ECDSA P-256, checked by Windows CNG), and the installer only when its size
and SHA-256 match. **Download and Install** runs the installer silently (`/S /RUN`).

Signing a release: `python -I tools/sign_update.py x.y.z "release notes"` with the private key
made once by `tools/make_update_key.py` (kept outside the repository). A fork that publishes its
own builds makes its own key and replaces `src/UpdateKey.h`.

## Linux

Both packages build in Docker, so any machine with Docker can build them.

**.deb for Ubuntu 26.04 and newer** (uses the system's Qt, libmpv, OpenImageIO, OpenColorIO):

```bash
docker build -t coreplay-linux-build tools/linux
docker run --rm -v "$PWD:/src:ro" -v "$PWD/dist/linux:/out" coreplay-linux-build bash /src/build-linux.sh --deb
```

**AppImage for Rocky/Alma 9, Ubuntu 22.04+, Debian 12+ and newer** (AlmaLinux 9 base, glibc 2.34;
bundles Qt, libmpv + ffmpeg, and OpenEXR / OpenImageIO / OpenColorIO built from upstream at the
same versions as the Windows build):

```bash
docker build -t coreplay-appimage-build -f tools/linux/appimage.Dockerfile tools/linux
docker run --rm -v "$PWD:/src:ro" -v "$PWD/dist/linux:/out" coreplay-appimage-build bash /src/build-appimage.sh
```

Add `--tests` to either script for a test build. On Linux there is no update check (the feed
carries the Windows installer); new versions ship as new packages.

## End-to-end tests

The test build adds a driver (`--test-script`). It runs the scripts in `tests/e2e/` against the
real window: hotkeys resolve through the same action table the keyboard uses (`keymap` checks
that no key has two actions), then the script checks the player's state, the libmpv properties
and grabbed pictures. The window opens without taking keyboard focus, and test runs use their
own settings (`CoreplayTest`).

```bash
tests/e2e/make_media.sh /path/to/media          # ffmpeg + the test build's mkexr
COREPLAY_TEST_MEDIA=/path/to/media COREPLAY_TEST_OUT=/path/to/out tests/e2e/run.sh
```

`COREPLAY_BUILD=<folder>` runs another build (for example a Linux one). Checks that need an
NVIDIA GPU (hardware decoding, NVENC export) fail on machines without one.

`COREPLAY_MPV_LOG=<file>` writes the player's full libmpv log, and the export's to `<file>.export`.

## Icon

`res/coreplay.ico` / `res/coreplay.png`: made by `python -I tools/make_icon.py`; embedded in the
.exe (`res/coreplay.rc`) and used as the window icon (`res/coreplay.qrc`).
