# Windows build: hand-off notes

Written on the Mac on 2026-11-26, for the Claude Code session that builds Rave It Out on the Windows PC.
This repo (`raveitoutofficial/raveitout-engine`, branch **`arm64-macos`**) is the engine plus the
RaveItOut theme. The Mac build is native arm64; this is the first **Windows x64** build.

## Goal

- One **64-bit Windows** build that runs on **Windows 10 and 11** (the release target), using the **OpenGL**
  renderer. The same build serves every PC; per-machine differences are only settings.
- Built and first tested on the user's own PC: Windows 11, RTX 3060 Ti, modern Intel i3, 1080p monitor.
- Later target: a friend's **Lenovo ideacentre AIO 700-22ISH** (Pentium G4400T 2C/2T, Intel HD 510, 8 GB,
  SSD, Windows 10 IoT LTSC) driving a Pump It Up **SD cabinet CRT at 640×480 @ 60 Hz** through an active
  HDMI-to-VGA converter, with a **PIUIO** I/O board.
- Public preview deadline: **2026-12-31**. No songs ship in releases (they're in `Songs/` locally only).

## Toolchain to install on the PC

- Git
- Visual Studio 2022 Build Tools: workload "Desktop development with C++" (MSVC v143, Windows SDK)
- CMake (3.20 or newer)

Build in a separate folder, for example `cmake -S . -B Build/win64 -G "Visual Studio 17 2022" -A x64`,
then `cmake --build Build/win64 --config Release`. **Ignore the README's Windows XP / VS2013 / `v140_xp`
advice:** that's from the older fork, and XP is not a target.

## Known hurdles (expect these first)

1. **FFmpeg libraries are 32-bit and old.** `extern/ffmpeg/lib` holds Win32 import libs for FFmpeg 2.1.3
   (Zeranoe build), and `Program/` holds the matching 32-bit DLLs (`avcodec-55.dll` etc.). They can't link
   into an x64 build. The Mac build uses **FFmpeg 5.1** (`extern/ffmpeg-5.1`, with `ffmpeg-arm64-video.patch`
   applied in the parent folder), so the engine's movie code targets the 5.1 API. Plan: use a prebuilt
   **x64 FFmpeg 5.1 "shared" LGPL** build (headers + .lib + DLLs), point `extern/ffmpeg` at it (or adjust
   `StepmaniaCore.cmake` around the `find_library(... "${SM_EXTERN_DIR}/ffmpeg/lib")` block), and ship the
   x64 DLLs in `Program/`. Keep it LGPL (no GPL-only components). Record the exact source and version.
2. **Other 32-bit DLLs in `Program/`:** `libusb0.dll`, `parallel_lights_io.dll`. Replace or drop them for x64.
3. **PIUIO (pads + lights) is Mac-only in CMake.** `src/arch/InputHandler/InputHandler_PIUIO_Libusb.cpp`
   (our native driver, libusb-1.0) is only added under `elseif(APPLE)` in `src/CMakeData-arch.cmake`. For
   Windows: add it to the Windows input sources, link an x64 **libusb-1.0** and ship its DLL. On Windows the
   board (`0547:1002`) also needs the **WinUSB** driver assigned once: Zadig for the first test; later a
   built-in one-click setup using **libwdi**. Pad lamps already work in the driver; cabinet lamps (marquee,
   bass neon) are not mapped yet (see `LightsDriver_PIUIOWrite.cpp` for known bit positions). Untested on
   real hardware.
4. **The Direct3D 9 renderer** (`RageDisplay_D3D.cpp`) also exists. Keep it building: on the Lenovo we'll
   compare OpenGL vs D3D9 frame rates (Intel's Windows OpenGL driver is the weak spot).

## Planned Windows work (in TODO.md on the Mac, summarized)

- **Display presets** in Graphics Options: CRT 640×480 4:3 / HD 1280×720 / Full HD 1920×1080 (each sets
  resolution + aspect + refresh together). HD/Full HD may follow the monitor's refresh; CRT stays 60 Hz.
- **CRT safety:** "keep this mode?" with auto-revert after ~10 s, and a startup fallback to the CRT preset.
  The cabinet's old PC goes black and needs a full power-off whenever anything leaves 640×480. This matters.
- **Low-end preset** for the Lenovo: MSAA off or 2x (pref `MSAASamples`), background videos re-encoded to
  640×480, a 2D noteskin fallback if `rio-3d` is too slow.
- **Kiosk setup** on the cabinet (Windows side, not code): auto sign-in into RIO, desktop at 640×480, updates
  paused during play.

## Things already true in this branch (don't redo)

- Default modifiers: AV 300, `rio-3d` noteskin, 30% mini (RIO's normal size), set in
  `Themes/RaveItOut/Scripts/99 InitGame.lua`.
- `MSAASamples` preference exists (default 4); the multisample code path is macOS-only so far.
- Separate ITG and Pump modifier lists in the song-select options list; Reset keeps speed and noteskin.
- Fixes: chained hold judgments, crash on quit mid-song, results-screen speed/noteskin display.
- `build-mac.sh` / `build.sh` are Mac-only. Write a Windows equivalent (for example `build-windows.ps1`)
  once the build works, so others can reproduce it.

## Rules the user set

- The project is open source under the **Unlicense**, free forever, and **never takes money** (no
  donations, Patreon or paywalls).
- **Commit locally; push only when the user says so.** Use the user's GitHub noreply email for commits.
- Theme files use **CRLF** line endings; keep them that way.
- No songs, official Pump It Up art or Andamiro material in the repo or releases.
- Timing, vsync and audio-sync checks must be done sitting at the PC, not over a Parsec stream.
- Songs for testing come over separately on USB (about 1.6 GB); copy them into `Songs/` on the PC.
