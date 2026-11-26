#!/usr/bin/env python3
"""Mirror a Milkshape ASCII model vertically (Y -> -Y), in place.

The engine's 3D models use Y pointing UP (unlike 2D sprites, where Y points
down): ITG's Down tap note has its tip at negative Y. Models built Y-down come
out upside down, so this flips them. Texture coordinates are left alone, and
each triangle's winding is reversed so faces stay front-facing.

  flip_y.py MODEL.txt [MODEL.txt ...]
"""
import sys


def flip(path):
    lines = open(path, newline="").read().split("\r\n" if "\r\n" in open(path, newline="").read() else "\n")
    out, i, n = [], 0, len(lines)
    while i < n:
        line = lines[i]
        out.append(line)
        # A mesh header looks like: "name" flags materialIndex
        if line.startswith('"') and i + 1 < n and lines[i + 1].strip().isdigit() and not lines[i - 1].startswith("Materials"):
            i += 1
            nv = int(lines[i]); out.append(lines[i]); i += 1
            for _ in range(nv):                      # flags x y z u v bone
                p = lines[i].split()
                p[2] = "%.5f" % -float(p[2])
                out.append(" ".join(p)); i += 1
            nn = int(lines[i]); out.append(lines[i]); i += 1
            for _ in range(nn):                      # nx ny nz
                p = lines[i].split()
                p[1] = "%.5f" % -float(p[1])
                out.append(" ".join(p)); i += 1
            nt = int(lines[i]); out.append(lines[i]); i += 1
            for _ in range(nt):                      # flags v1 v2 v3 n1 n2 n3 group
                p = lines[i].split()
                p[2], p[3] = p[3], p[2]
                p[5], p[6] = p[6], p[5]
                out.append(" ".join(p)); i += 1
            continue
        i += 1
    sep = "\r\n" if "\r\n" in open(path, newline="").read() else "\n"
    open(path, "w", newline="").write(sep.join(out))


if __name__ == "__main__":
    for p in sys.argv[1:]:
        flip(p)
        print("flipped", p)
