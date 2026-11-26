# Windows build: hand-off notes

## Status (2026-11-26, branch `windows-x64`): it builds and runs

First working **Windows x64** build, made and tested on the Lenovo AIO 700-22ISH (Pentium G4400T, Intel
HD 510, Windows 11 Home). Plays songs with OpenGL and D3D9; `rio-3d` + MSAA 2x holds a locked 60 FPS at
720p and 640x480 (about 58 FPS at 1080p) on Overcompensate S17 with its 1080p video.

How to build (VS 2022 Build Tools + CMake 4.x):

```
cmake -S . -B Build/win64 -G "Visual Studio 17 2022" -A x64 -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build Build/win64 --config Release
```

Not in git (as on the Mac): put **FFmpeg 5.1.6 win64 LGPL shared** (BtbN build
`ffmpeg-n5.1.6-16-g6e63e49496-win64-lgpl-shared-5.1.zip`, release `autobuild-2025-01-31-12-58`) in
`extern/ffmpeg-5.1-win64/` (`include`, `lib`, `bin`), and copy its `bin/*.dll` next to `Program/StepMania.exe`.

What changed for x64 / modern MSVC:
- 64-bit builds use `extern/ffmpeg-5.1-win64` (`SM_WIN_FFMPEG_DIR`) and libusb-win32 1.4.0.2 x64
  (`extern/libusb/lib/msvc_x64`; `Program/libusb0.dll` is now the x64 DLL). The 32-bit `ddk` libs are 32-bit only.
- No legacy DirectX SDK needed: D3DX/DxErr replaced (error strings via FormatMessage, screenshots via
  GetRenderTargetData). **D3D texture handles were pointers truncated to 32 bits (crash) — now a handle table.**
- C++17 for MSVC, with `_HAS_AUTO_PTR_ETC=1` and `_HAS_STD_BYTE=0`; libmad uses `FPM_64BIT` on x64; tomcrypt's
  `SHA1`/`SHA256` macros hidden around `windows.h`.
- x64 fixes: inline asm (`global.cpp`, crash handler), `INT_PTR` dialog procs, `SetClassLongPtr`, MIDI callback
  `DWORD_PTR`, crash-report addresses; x64 crash backtraces use `RtlVirtualUnwind`.
- **MSAA on Windows OpenGL** (was macOS-only): `wglChoosePixelFormatARB` with `MSAASamples` (0/2/4/8).
- Log line `Vsync: requested swap interval N, driver reports M` (diagnoses drivers that ignore vsync).
- Debug switch: env var `SM_NO_CRASH_HANDLER=1` writes `crash-min.txt` (crash address + stack, look up in
  `StepMania.map`) instead of using the crash handler.

Lessons from the Lenovo:
- Update the Intel driver. The 2016 Lenovo driver ignored vsync (tearing) and ran far more frames over budget.
  Intel's installer refuses OEM-locked drivers; install 31.0.101.2115 via Device Manager > Have Disk.
- Fork default: all Intel GPUs get `d3d,opengl` + 16-bit textures (`StepMania.cpp`); HD 510 runs OpenGL fine.
- Anything else drawing on an integrated GPU (Claude app, remote/computer-use tools) badly skews FPS tests.

Not done yet: PIUIO on Windows via libusb-1.0, display presets + CRT safety, an SD (640x480) theme with SD-made
fonts/assets, `build-windows.ps1`.

---

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
