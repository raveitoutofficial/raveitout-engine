#!/usr/bin/env python3
"""Cut a tap note into the 3x3 grid of pieces the 2D "crack" explosion uses.

The 2D rio explosion crops 9 copies of the tap note sprite (thirds across and
down) and throws them. Cropping doesn't work on 3D models, so this writes one
outline per grid cell; noteskin3d_blender.py then builds each as its own 3D
piece. Pieces keep their original position and UVs, so together they look like
the whole arrow until the explosion moves them apart.

  noteskin3d_pieces.py SHEET.png COLS ROWS OUT_DIR NAME
Writes OUT_DIR/_NAMEPiece_<key> outline.json for keys
  upleft up upright left center right downleft down downright
(cells with no art are skipped). Needs numpy, pillow, scikit-image.
"""
import json, os, sys
import numpy as np
from PIL import Image
from skimage import measure

KEYS = [["upleft", "up", "upright"], ["left", "center", "right"], ["downleft", "down", "downright"]]


def main():
    sheet, cols, rows, out_dir, name = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4], sys.argv[5]
    img = np.array(Image.open(sheet).convert("RGBA"))
    fh, fw = img.shape[0] // rows, img.shape[1] // cols
    mask = np.zeros((fh, fw), bool)
    for r in range(rows):
        for c in range(cols):
            mask |= img[r * fh:(r + 1) * fh, c * fw:(c + 1) * fw, 3] > 128

    xs = [0, round(fw / 3), round(2 * fw / 3), fw]
    ys = [0, round(fh / 3), round(2 * fh / 3), fh]
    made = []
    for gy in range(3):
        for gx in range(3):
            cell = np.zeros_like(mask)
            cell[ys[gy]:ys[gy + 1], xs[gx]:xs[gx + 1]] = True
            piece = mask & cell
            if piece.sum() < 20:
                continue
            # Largest connected part of the piece (tiny slivers are dropped).
            lab = measure.label(piece)
            biggest = max(measure.regionprops(lab), key=lambda r: r.area).label
            piece = lab == biggest
            cont = max(measure.find_contours(np.pad(piece.astype(float), 1), 0.5), key=len)
            cont = measure.approximate_polygon(cont, 0.8)
            pts = np.stack([cont[:, 1] - 1, cont[:, 0] - 1], axis=1)
            if np.allclose(pts[0], pts[-1]):
                pts = pts[:-1]
            area = 0.5 * np.sum(pts[:, 0] * np.roll(pts[:, 1], -1) - np.roll(pts[:, 0], -1) * pts[:, 1])
            if area > 0:
                pts = pts[::-1]
            key = KEYS[gy][gx]
            with open(os.path.join(out_dir, "_%sPiece_%s outline.json" % (name, key)), "w") as f:
                json.dump({"width": fw, "height": fh, "points": pts.tolist()}, f)
            made.append(key)
    print("%s: %d pieces (%s)" % (name, len(made), " ".join(made)))


if __name__ == "__main__":
    main()
