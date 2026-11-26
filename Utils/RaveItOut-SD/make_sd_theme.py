#!/usr/bin/env python3
"""Generate Themes/RaveItOut-SD (640x480, 4:3, 31 kHz CRT) from the HD theme.

The HD theme (Themes/RaveItOut) is the single master. This script builds a
complete SD copy of it:

  * Fonts   - every font page is Lanczos-downscaled to roughly the pixel size it
              is drawn at in SD; with mipmaps (see below) text drawn at
              small zooms is filtered instead of aliasing.
  * Images  - art bigger than the screen in both directions is scaled down to
              screen size; every texture gets mipmaps through the "-mipmaps" folder name.
  * Videos  - 16:9 background/intro videos are centre-cropped to 4:3 and scaled
              to 640x480 (what Cover() shows on a 4:3 screen anyway), which
              also cuts decode cost on the cabinet PC.
  * overlay/ (next to this script) is copied on top last: hand-made SD files
              such as ThemeInfo.ini and 4:3 layout fixes.

Virtual sizes never change: a downscaled file is renamed with a
"(res WxH)" hint giving its original virtual size, so every zoom/position in
the Lua stays valid. References to renamed files that include the extension
are rewritten in the copied .lua/.ini/.redir/.xml files.

The theme's virtual canvas is 480 units tall (_fallback ScreenHeight=480), so
at 640x480 one unit is one pixel; "virtual size" == "pixels on the CRT".

Usage:  python make_sd_theme.py [--ffmpeg PATH] [--force] [--skip-videos]
Reruns are incremental: outputs newer than their source are kept.
"""
import argparse, os, re, shutil, struct, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
SRC = os.path.join(REPO, "Themes", "RaveItOut")
# "mipmaps" in the folder name is deliberate: the engine reads texture hints
# from the whole path, so every texture of this theme gets mipmaps (clean
# minification of text/art drawn at zoom < 1) without renaming files.
DST = os.path.join(REPO, "Themes", "RaveItOut-SD-mipmaps")
OVERLAY = os.path.join(HERE, "overlay")
MARKER = "_GENERATED-BY-make_sd_theme.txt"

SCREEN_W, SCREEN_H = 854, 480          # widest virtual screen the theme is drawn on (16:9 @ 480)
SD_W, SD_H = 640, 480

IMAGE_EXT = (".png", ".jpg", ".jpeg", ".bmp")
VIDEO_EXT = (".mp4", ".avi", ".mpg", ".mpeg", ".mkv", ".webm", ".ogv", ".wmv", ".m4v", ".flv")
TEXT_EXT = (".lua", ".ini", ".redir", ".xml", ".txt")

# Font pixel scale, picked from how big each font is drawn in the Lua
# (grep the zooms). Scale is relative to the font's virtual size.
# Anything not listed: 1.0 (mipmaps only).
FONT_SCALE = [
    (r"montserrat .* 60px", 0.6),   # drawn mostly at zoom 0.5-0.6, down to 0.2
    (r"bebas neue bold 90px", 0.55),# mostly 0.52
    (r"bebas neue 40px", 0.5),
    (r"zona pro (bold|thin) 40px", 0.5),
]

# Images left unscaled (and unrenamed) because Lua builds their names at
# runtime, e.g. GetPathG("FastSlow", j.." 2x1 (doubleres).png").
IMAGE_KEEP = [r"FastSlow", r"SortOrder", r"avatars", r"ProfileBanner"]

# Videos left alone (drawn with a plain zoom, so their pixel size matters).
VIDEO_KEEP = [r"PlayModeIcons"]


def log(*a):
    print(*a, flush=True)


def find_ffmpeg(arg):
    cands = [arg] if arg else []
    cands += [os.path.join(REPO, "..", "_deps", "ffmpeg", d, "bin", "ffmpeg.exe")
              for d in (os.listdir(os.path.join(REPO, "..", "_deps", "ffmpeg"))
                        if os.path.isdir(os.path.join(REPO, "..", "_deps", "ffmpeg")) else [])]
    cands += [shutil.which("ffmpeg") or ""]
    for c in cands:
        if c and os.path.isfile(c):
            return os.path.abspath(c)
    sys.exit("ffmpeg not found; pass --ffmpeg PATH")


def image_size(path, ffprobe):
    with open(path, "rb") as f:
        head = f.read(24)
    if head[:8] == b"\x89PNG\r\n\x1a\n":
        return struct.unpack(">II", head[16:24])
    out = subprocess.run([ffprobe, "-v", "error", "-select_streams", "v:0", "-show_entries",
                          "stream=width,height", "-of", "csv=p=0", path],
                         capture_output=True, text=True).stdout.strip().split(",")
    return int(out[0]), int(out[1])


def video_info(path, ffprobe):
    out = subprocess.run([ffprobe, "-v", "error", "-select_streams", "v:0", "-show_entries",
                          "stream=width,height,r_frame_rate", "-of", "csv=p=0", path],
                         capture_output=True, text=True).stdout.strip().splitlines()[0].split(",")
    return int(out[0]), int(out[1]), out[2]


def frames_of(name):
    m = re.search(r" ([0-9]+)x([0-9]+)([\. ]|$)", name)
    return (int(m.group(1)), int(m.group(2))) if m else (1, 1)


def up_to_date(src, dst, force, strict=False):
    # strict: dst must be newer than src. Plain copies (copy2) keep src's mtime,
    # so they never count as an already-converted video.
    if force or not os.path.exists(dst):
        return False
    d, s = os.path.getmtime(dst), os.path.getmtime(src)
    return d > s if strict else d >= s


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        log("  ffmpeg failed:", " ".join(cmd[-3:]), "\n", r.stderr[-800:])
        return False
    return True


def scale_image(ffmpeg, src, dst, w, h):
    # Premultiply so transparent pixels don't bleed dark fringes into edges.
    vf = ("format=rgba,premultiply=inplace=1,"
          f"scale={w}:{h}:flags=lanczos+accurate_rnd+full_chroma_int,"
          "unpremultiply=inplace=1")
    ext = os.path.splitext(dst)[1].lower()
    cmd = [ffmpeg, "-v", "error", "-y", "-i", src, "-vf", vf]
    if ext in (".jpg", ".jpeg"):
        cmd = [ffmpeg, "-v", "error", "-y", "-i", src, "-vf",
               f"scale={w}:{h}:flags=lanczos+accurate_rnd+full_chroma_int", "-q:v", "2"]
    else:
        cmd += ["-pix_fmt", "rgba"]
    return run(cmd + [dst])


def plan_image(rel, w, h):
    """Return (new_basename, out_w, out_h) for an image."""
    base = os.path.basename(rel)
    stem, ext = os.path.splitext(base)
    low = stem.lower()
    doubleres = "(doubleres)" in low
    vw, vh = (w // 2, h // 2) if doubleres else (w, h)   # virtual size in units

    is_font = rel.replace("\\", "/").lower().startswith("fonts/")
    s = 1.0
    if is_font:
        for pat, fs in FONT_SCALE:
            if re.search(pat, low):
                s = fs
                break
    elif vw > SCREEN_W and vh > SCREEN_H:
        s = max(SCREEN_W / vw, SCREEN_H / vh)    # fits the screen in one direction

    fx, fy = frames_of(stem)
    ow = max(fx, round(vw / fx * s) * fx)
    oh = max(fy, round(vh / fy * s) * fy)

    # Non-font doubleres art keeps its name: Lua and .redir files refer to
    # these names in many dynamic ways. The engine halves them at load; mipmaps apply.
    if doubleres and not is_font:
        return base, w, h, False
    if any(re.search(p, rel.replace("\\", "/")) for p in IMAGE_KEEP):
        return base, w, h, False
    if (ow, oh) == (w, h):
        return base, w, h, False
    stem2 = re.sub(r"\s*\(doubleres\)", "", stem, flags=re.I)
    hints = ""
    if (ow, oh) != (vw, vh):
        hints += f" (res {vw}x{vh})"
    return stem2 + hints + ext, ow, oh, (ow, oh) != (w, h)


def encode_video(ffmpeg, src, dst, w, h, fps):
    vf = []
    if w / h > 4 / 3 + 0.02:                  # wider than 4:3: crop centre (what Cover shows)
        cw = round(h * 4 / 3 / 2) * 2
        vf.append(f"crop={cw}:{h}")
    vf.append(f"scale={SD_W}:{SD_H}:flags=lanczos,setsar=1")
    cmd = [ffmpeg, "-v", "error", "-y", "-i", src, "-an", "-vf", ",".join(vf),
           "-c:v", "mpeg2video", "-q:v", "3", "-g", "30", "-bf", "2", "-r", fps, dst]
    return run(cmd)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ffmpeg")
    ap.add_argument("--force", action="store_true", help="re-encode everything")
    ap.add_argument("--skip-videos", action="store_true", help="copy videos unchanged")
    args = ap.parse_args()

    ffmpeg = find_ffmpeg(args.ffmpeg)
    ffprobe = os.path.join(os.path.dirname(ffmpeg), "ffprobe" + os.path.splitext(ffmpeg)[1])

    if os.path.isdir(DST) and not os.path.exists(os.path.join(DST, MARKER)):
        sys.exit(f"{DST} exists but was not made by this script; refusing to touch it.")
    os.makedirs(DST, exist_ok=True)
    with open(os.path.join(DST, MARKER), "w") as f:
        f.write("Generated from Themes/RaveItOut by Utils/RaveItOut-SD/make_sd_theme.py.\n"
                "Do not edit here: change the HD theme or Utils/RaveItOut-SD/overlay and rerun.\n")

    produced = {os.path.normcase(MARKER)}
    renames = {}          # old basename (lower) -> set of new basenames
    renames_by_rel = {}   # normcase(rel path) -> new basename
    stats = dict(fonts=0, images=0, scaled=0, videos=0, copied=0)

    for root, dirs, files in os.walk(SRC):
        dirs[:] = [d for d in dirs if d != ".git"]
        for fn in files:
            src = os.path.join(root, fn)
            rel = os.path.relpath(src, SRC)
            ext = os.path.splitext(fn)[1].lower()
            reldir = os.path.dirname(rel)
            os.makedirs(os.path.join(DST, reldir), exist_ok=True)

            if ext in IMAGE_EXT:
                w, h = image_size(src, ffprobe)
                newbase, ow, oh, needs_scale = plan_image(rel, w, h)
                out_rel = os.path.join(reldir, newbase)
                dst = os.path.join(DST, out_rel)
                if not up_to_date(src, dst, args.force):
                    if needs_scale:
                        if not scale_image(ffmpeg, src, dst, ow, oh):
                            shutil.copy2(src, dst)
                    else:
                        shutil.copy2(src, dst)
                    os.utime(dst)
                stats["fonts" if rel.lower().startswith("fonts") else "images"] += 1
                stats["scaled"] += needs_scale
                if newbase != fn:
                    renames.setdefault(fn.lower(), set()).add(newbase)
                    renames_by_rel[os.path.normcase(rel)] = newbase
                produced.add(os.path.normcase(out_rel))
                continue

            dst = os.path.join(DST, rel)
            if ext in VIDEO_EXT and not args.skip_videos and \
                    not any(re.search(p, rel) for p in VIDEO_KEEP):
                if not up_to_date(src, dst, args.force, strict=True):
                    w, h, fps = video_info(src, ffprobe)
                    log(f"  video {rel} {w}x{h} -> {SD_W}x{SD_H}")
                    tmp = dst + ".tmp" + ext
                    if encode_video(ffmpeg, src, tmp, w, h, fps):
                        os.replace(tmp, dst)
                    else:
                        shutil.copy2(src, dst)
                stats["videos"] += 1
            elif not up_to_date(src, dst, args.force) or ext in TEXT_EXT:
                shutil.copy2(src, dst)
            stats["copied"] += 1
            produced.add(os.path.normcase(rel))

    # Rewrite quoted references that name a renamed image with its extension.
    # Resolve like the engine: relative to the referencing file first, then by
    # theme-path suffix ("Graphics/foo.png", "_BGMovies/x.png", ...).
    rx = re.compile(r"""(["'])([^"'\r\n]*?)([^"'/\\\r\n]+\.(?:png|jpe?g|bmp))\1""", re.I)
    unresolved = set()
    rewritten = 0
    for p in list(produced):
        if p == os.path.normcase(MARKER) or os.path.splitext(p)[1].lower() not in TEXT_EXT:
            continue
        path = os.path.join(DST, p)
        with open(path, "rb") as f:
            text = f.read().decode("latin-1")     # byte-exact round trip, keeps CRLF
        here = os.path.dirname(p)

        def sub(m):
            q, prefix, base = m.group(1), m.group(2), m.group(3)
            if base.lower() not in renames:
                return m.group(0)
            want = (prefix + base).replace("\\", "/").lower()
            local = os.path.normcase(os.path.normpath(os.path.join(here, prefix + base)))
            if local in renames_by_rel:
                hits = {renames_by_rel[local]}
            else:
                hits = {v for k, v in renames_by_rel.items()
                        if k.replace("\\", "/").endswith(("/" + want.lstrip("/"), " " + want.lstrip("/")))
                        or k.replace("\\", "/") == want}
            if len(hits) != 1:
                unresolved.add(f"{p}: {prefix}{base}")
                return m.group(0)
            return q + prefix + hits.pop() + q
        new = rx.sub(sub, text)
        if new != text:
            with open(path, "wb") as f:
                f.write(new.encode("latin-1"))
            rewritten += 1

    # Overlay: hand-made SD files win over generated ones.
    for root, dirs, files in os.walk(OVERLAY):
        for fn in files:
            src = os.path.join(root, fn)
            rel = os.path.relpath(src, OVERLAY)
            os.makedirs(os.path.join(DST, os.path.dirname(rel)), exist_ok=True)
            shutil.copy2(src, os.path.join(DST, rel))
            produced.add(os.path.normcase(rel))

    # Remove stale outputs (files from older runs whose source is gone/renamed).
    stale = 0
    for root, dirs, files in os.walk(DST, topdown=False):
        for fn in files:
            rel = os.path.normcase(os.path.relpath(os.path.join(root, fn), DST))
            if rel not in produced:
                os.remove(os.path.join(root, fn))
                stale += 1
        if not os.listdir(root):
            os.rmdir(root)

    log(f"done: {stats}, {rewritten} text files rewritten, {stale} stale removed")
    for u in sorted(unresolved):
        log("  unresolved image reference (check by hand):", u)


if __name__ == "__main__":
    main()
