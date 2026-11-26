# Rave It Out — SD theme (640×480, 4:3, 31 kHz CRT)

`Themes/RaveItOut-SD-mipmaps` is **generated** from the HD theme `Themes/RaveItOut`.
Never edit it by hand: change the HD theme (the only master art) or `overlay/`, then regenerate.

## Regenerate

```
_deps/python/python.exe sm-rio/Utils/RaveItOut-SD/make_sd_theme.py
```

Any Python 3 works (no extra modules). It needs ffmpeg + ffprobe: by default it uses
`_deps/ffmpeg/*/bin/ffmpeg.exe` next to `sm-rio`, otherwise `ffmpeg` on PATH, or pass `--ffmpeg PATH`.

* Incremental: only changed sources are redone. Use `--force` after changing the scale
  tables in the script (`FONT_SCALE`, `IMAGE_KEEP`, `VIDEO_KEEP`).
* `--skip-videos` copies the videos unchanged (fast run). Encoding all the videos takes a few minutes on the Lenovo.
* Files that no longer have a source are deleted from the SD theme.

In game: Options → Display/Theme → "Rave It Out: Miami Nights (SD 640x480)",
or set `Theme=RaveItOut-SD-mipmaps` in `Save/Preferences.ini`.

## How it works

* The theme's virtual canvas is 480 units tall (`_fallback` `[Common] ScreenHeight=480`,
  width follows the aspect: 640 at 4:3, 854 at 16:9). At 640×480 one unit is one pixel,
  so nothing is "shrunk" globally. The HD look comes from big textures (fonts rendered at
  60–90px, 1280×720 art) drawn with `zoom` 0.2–0.6. Without mipmaps that minification aliases.
* **Mipmaps for everything:** the engine looks for texture hints in the *whole path*
  (`RageBitmapTexture.cpp`, `sHintString.find("mipmaps")`), so the folder name ending in
  `-mipmaps` turns on mipmaps for every texture in the theme without renaming files.
  Together with `TrilinearFiltering=1` this is what makes small text clean. Don't rename the folder.
* **Fonts:** pages are Lanczos-scaled to about the size they are drawn at (`FONT_SCALE`).
  The `(res WxH)` hint keeps the original virtual size, so the font `.ini` metrics and every zoom stay valid.
* **Images:** `(doubleres)` art is pre-halved with Lanczos (at 480p the engine would halve it
  at load time with a cheaper filter). Art bigger than the screen in both directions is scaled down to screen size,
  renamed with `(res WxH)`. Quoted references with an extension are rewritten in the copied Lua.
  Files whose names Lua builds at runtime are listed in `IMAGE_KEEP` and left alone.
* **Videos:** 16:9 videos are centre-cropped to 4:3 (what `Cover()` shows on a 4:3 screen) and
  re-encoded as MPEG-2 640×480 without audio. That is cheaper to decode on the cabinet PC.
  `PlayModeIcons` are drawn with a plain zoom, so they keep their size (`VIDEO_KEEP`).
* **overlay/** is copied on top last: `ThemeInfo.ini` (SD display name) and any SD-only
  Lua/metrics fixes for the 4:3 layout.

The script prints "unresolved image reference" for references it couldn't map. Check those by hand
(add an `IMAGE_KEEP` pattern or an overlay file).
