#!/usr/bin/env python3
"""Prep step for tools/noteskin3d_blender.py (beveled 3D tap notes).

  noteskin3d_prep.py SHEET.png COLS ROWS OUT_DIR NAME

Writes into OUT_DIR:
  _NAME outline.json       arrow outline in pixel coords + frame size
  _NAME frame N.png        sprite frames, made opaque with the art's colours
                           extended a few pixels past the edge (so beveled
                           and side faces never sample transparent pixels)
  _NAME tex.ini            animated texture cycling the frames
  _NAME rim 0/1/2.png      side-wall colours (light, mid, dark)
  _shine sphere.png        additive sphere-map highlight (shared; the engine
                           sphere-maps any texture whose name contains "sphere")

Needs numpy, pillow, scikit-image (scipy comes with it).
"""
import argparse, json, os
import numpy as np
from PIL import Image
from scipy import ndimage
from skimage import measure


def outline(mask, tolerance):
    c = max(measure.find_contours(np.pad(mask.astype(float), 1), 0.5), key=len)
    c = measure.approximate_polygon(c, tolerance)
    pts = np.stack([c[:, 1] - 1, c[:, 0] - 1], axis=1)
    if np.allclose(pts[0], pts[-1]):
        pts = pts[:-1]
    area = 0.5 * np.sum(pts[:, 0] * np.roll(pts[:, 1], -1) - np.roll(pts[:, 0], -1) * pts[:, 1])
    if area > 0:  # make counter-clockwise in screen space (y down)
        pts = pts[::-1]
    return pts.tolist()


def pad_colours(frame, mask, grow=6):
    """Opaque copy: every pixel near the shape takes the colour of the nearest
    solid pixel of the art."""
    solid = frame[:, :, 3] > 200
    _, (iy, ix) = ndimage.distance_transform_edt(~solid, return_indices=True)
    out = frame[iy, ix].copy()
    keep = ndimage.binary_dilation(mask, iterations=grow)
    inside = solid | (keep & ~solid)
    # Inside the shape keep the original art (premultiplied onto its own
    # edge colour where it was semi-transparent); outside, the padded colour.
    a = frame[:, :, 3:4].astype(float) / 255.0
    blend = (frame[:, :, :3] * a + out[:, :, :3] * (1 - a)).astype(np.uint8)
    out[:, :, :3] = np.where(solid[:, :, None], frame[:, :, :3], blend)
    out[:, :, 3] = np.where(inside, 255, 0)
    return out


def sphere_map(size=256):
    """Black (adds nothing) where the surface faces the viewer; bright
    highlights toward the rim and from the upper left."""
    y, x = np.mgrid[-1:1:size * 1j, -1:1:size * 1j]
    r2 = x * x + y * y
    inside = r2 <= 1
    z = np.sqrt(np.clip(1 - r2, 0, 1))
    n = np.stack([x, -y, z], axis=-1)
    light = np.array([-0.5, 0.6, 0.62]); light /= np.linalg.norm(light)
    view = np.array([0, 0, 1.0])
    h = light + view; h /= np.linalg.norm(h)
    spec = np.clip((n * h).sum(-1), 0, 1) ** 40
    rim = np.clip(1 - z, 0, 1) ** 2.5 * 0.55
    v = np.clip(spec * 1.1 + rim, 0, 1)
    v = np.where(inside, v, 0)
    img = (np.stack([v, v, v], -1) * 255).astype(np.uint8)
    return Image.fromarray(img, "RGB")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("sheet"); ap.add_argument("cols", type=int); ap.add_argument("rows", type=int)
    ap.add_argument("out_dir"); ap.add_argument("name")
    ap.add_argument("--tolerance", type=float, default=0.8)
    ap.add_argument("--frame-seconds", type=float, default=1 / 6.0)
    a = ap.parse_args()

    sheet = np.array(Image.open(a.sheet).convert("RGBA"))
    fh, fw = sheet.shape[0] // a.rows, sheet.shape[1] // a.cols
    frames = [sheet[r * fh:(r + 1) * fh, c * fw:(c + 1) * fw] for r in range(a.rows) for c in range(a.cols)]
    mask = np.zeros((fh, fw), bool)
    for fr in frames:
        mask |= fr[:, :, 3] > 128

    os.makedirs(a.out_dir, exist_ok=True)
    ini = ["[AnimatedTexture]"]
    for i, fr in enumerate(frames):
        fn = "_%s frame %d.png" % (a.name, i)
        Image.fromarray(pad_colours(fr, mask)).save(os.path.join(a.out_dir, fn))
        ini += ["Frame%04d=%s" % (i, fn), "Delay%04d=%.4f" % (i, a.frame_seconds)]
    with open(os.path.join(a.out_dir, "_%s tex.ini" % a.name), "w", newline="\r\n") as f:
        f.write("\n".join(ini) + "\n")
    # Side-wall colours: the art's edge colour, shaded three ways (light /
    # mid / dark) so the walls read as solid even though the engine doesn't
    # light notes. Picked per face by the Blender step from its direction.
    edge = mask & ~ndimage.binary_erosion(mask, iterations=5)
    solid0 = frames[0][:, :, 3] > 200
    px = frames[0][edge & solid0][:, :3].astype(float)
    base = np.median(px, axis=0) if len(px) else np.array([128.0, 128.0, 128.0])
    for i, k in enumerate((1.3, 0.75, 0.42, 1.0)):  # 3 = plain edge colour (lit mode)
        c = np.clip(base * k + (40 if k > 1 else 0), 0, 255).astype(np.uint8)
        Image.fromarray(np.tile(c, (8, 8, 1))).save(os.path.join(a.out_dir, "_%s rim %d.png" % (a.name, i)))
    with open(os.path.join(a.out_dir, "_%s outline.json" % a.name), "w") as f:
        json.dump({"width": fw, "height": fh, "points": outline(mask, a.tolerance)}, f)
    sp = os.path.join(a.out_dir, "_shine sphere.png")
    if not os.path.exists(sp):
        sphere_map().save(sp)
    print("%s: prepped %d frames, outline ready" % (a.name, len(frames)))


if __name__ == "__main__":
    main()
