# Rave It Out Audio Sync — Windows

Windows port of the Mac companion app (`audio-sync/` on the Mac). Same behavior and the same files, so
profiles move between the two builds:

- One **global offset** per audio device, kept in `Save\AudioSyncProfiles.json` (same JSON the Mac writes,
  byte for byte).
- **Apply** writes `GlobalOffsetSeconds` in `Save\Preferences.ini`. Only that line changes; every other byte,
  including the CRLF line endings, stays the same. **Apply & Play** also starts `Program\StepMania.exe`.
- **Link** a profile to the current Windows sound output (default playback device); it's picked
  automatically when that device becomes the default (optionally applied too).
- **Save game value** turns the offset you found in-game into a new profile.
- Won't write while `StepMania.exe` is running: the game saves its preferences when it quits and would undo it.
- Finds the game by looking in its own folder (and a few levels up) for `Save\Preferences.ini`,
  `portable.ini` or `Program\StepMania.exe`; otherwise use **Change…**. App settings (auto-apply, close after
  launch, chosen folder) go in `%APPDATA%\Rave It Out Audio Sync\settings.ini`.
- Keys: Ctrl+S = Apply, Ctrl+Enter = Apply & Play.

Note: linked device names come from each OS (on Windows e.g. "Speakers (Realtek High Definition Audio)"),
so a link made on the Mac doesn't match on Windows. Offsets and names are shared.

## Why C# WinForms on .NET Framework 4.8

.NET Framework 4.8 is part of Windows 10/11, so nothing has to be installed. The exe is ~110 KB and starts
fast on the low-end cabinet PC (Pentium G4400T / HD 510). The default audio device comes from the Core Audio
COM API, which is built into Windows.

## Build

Needs Visual Studio 2022 (or its Build Tools) with MSBuild, which provides the C# compiler. No NuGet or project file.

    powershell -ExecutionPolicy Bypass -File Utils\AudioSync-Windows\build.ps1           # -> sm-rio\Rave It Out Audio Sync.exe
    powershell -ExecutionPolicy Bypass -File Utils\AudioSync-Windows\build.ps1 -OutDir <dir>
    powershell -ExecutionPolicy Bypass -File Utils\AudioSync-Windows\build.ps1 -Test     # + tests

`-Test` builds `AudioSyncTests.exe` into `%TEMP%\rio-audiosync-test` and runs it on **copies** of
`Save\Preferences.ini` and `Save\AudioSyncProfiles.json`. It checks that writing the original value back gives
identical bytes and that the Mac JSON re-encodes identically. It also covers a missing file and a missing key.

## Run

Put `Rave It Out Audio Sync.exe` in the game folder, next to `Program\` and `Save\` (the portable layout), and
double-click it. The exe is git-ignored (`*.exe`); `AppIcon.ico` was made from `audio-sync/Icon/icon_1024.png`.

Public domain (Unlicense), like the rest of the build.
