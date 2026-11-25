#!/usr/bin/env python3
"""Give 2D hold/roll bodies and bottom caps a rendered 3D (beveled) look.

The engine draws hold bodies as stretched texture strips, not models, so
instead of real geometry each sprite frame is rebuilt in Blender as a
beveled bar, lit, and rendered straight back to a texture of the same size.
Flat surfaces keep the art's exact colours; only the bevels get light and
shadow. Seen straight on (normal play) it looks like 3D at no extra cost.

  noteskin3d_holds.py SHEET.png COLS ROWS KIND OUT.png [--blender blender]
     KIND = body  (tiles vertically: wraps top/bottom so it stays seamless)
            cap   (bottom cap: continues the body at the top, shaped end)

Needs numpy, pillow, scikit-image; calls Blender headless for the renders.
"""
import argparse, json, os, subprocess, sys, tempfile
import numpy as np
from PIL import Image
from scipy import ndimage
from skimage import measure

PAD = 24  # extra rows above/below so top/bottom bevels fall outside the tile

BLENDER_JOB = r'''
import bpy, bmesh, json, math, sys
jobs = json.load(open(sys.argv[sys.argv.index("--") + 1]))
P = jobs["params"]
L = [-0.45, 0.45, 0.77]                      # light from upper left (Blender: +Y is up)
n = math.sqrt(sum(c * c for c in L)); L = [c / n for c in L]
Hv = [L[0], L[1], L[2] + 1]; n = math.sqrt(sum(c * c for c in Hv)); Hv = [c / n for c in Hv]

def material(tex_path, W, H):
    m = bpy.data.materials.new("m"); m.use_nodes = True
    nt = m.node_tree; nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    em = nt.nodes.new("ShaderNodeEmission")
    img = nt.nodes.new("ShaderNodeTexImage"); img.image = bpy.data.images.load(tex_path)
    img.interpolation = "Closest"; img.extension = "EXTEND"
    tc = nt.nodes.new("ShaderNodeTexCoord"); mp = nt.nodes.new("ShaderNodeMapping")
    mp.inputs["Scale"].default_value = (1.0 / W, 1.0 / H, 1)
    mp.inputs["Location"].default_value = (0, 1, 0)
    nt.links.new(tc.outputs["Object"], mp.inputs["Vector"]); nt.links.new(mp.outputs["Vector"], img.inputs["Vector"])
    geo = nt.nodes.new("ShaderNodeNewGeometry")
    def dot(vec):
        d = nt.nodes.new("ShaderNodeVectorMath"); d.operation = "DOT_PRODUCT"
        d.inputs[1].default_value = vec; nt.links.new(geo.outputs["Normal"], d.inputs[0]); return d
    def math_node(op, a, b=None):
        mn = nt.nodes.new("ShaderNodeMath"); mn.operation = op
        for i, v in enumerate((a, b)):
            if v is None: continue
            if isinstance(v, (int, float)): mn.inputs[i].default_value = v
            else: nt.links.new(v, mn.inputs[i])
        return mn.outputs[0]
    # diffuse: 1 on flat faces, darker/brighter on bevels
    dl = dot(L).outputs["Value"]
    shade = math_node("ADD", math_node("MULTIPLY", math_node("SUBTRACT", dl, L[2]), P["diffuse"]), 1.0)
    shade = math_node("MAXIMUM", shade, 0.3)
    # specular glint on bevels only
    dh = dot(Hv).outputs["Value"]
    spec = math_node("MAXIMUM", math_node("SUBTRACT", math_node("POWER", math_node("MAXIMUM", dh, 0.0), 30.0), Hv[2] ** 30), 0.0)
    spec = math_node("MULTIPLY", spec, P["spec"])
    col = nt.nodes.new("ShaderNodeMix"); col.data_type = "RGBA"; col.blend_type = "MULTIPLY"
    col.inputs["Factor"].default_value = 1.0
    nt.links.new(img.outputs["Color"], col.inputs[6])
    comb = nt.nodes.new("ShaderNodeCombineColor")
    for k in ("Red", "Green", "Blue"): nt.links.new(shade, comb.inputs[k])
    nt.links.new(comb.outputs["Color"], col.inputs[7])
    add = nt.nodes.new("ShaderNodeMix"); add.data_type = "RGBA"; add.blend_type = "ADD"
    nt.links.new(spec, add.inputs["Factor"]); nt.links.new(col.outputs[2], add.inputs[6])
    add.inputs[7].default_value = (1, 1, 1, 1)
    nt.links.new(add.outputs[2], em.inputs["Color"]); nt.links.new(em.outputs[0], out.inputs["Surface"])
    return m

scene = bpy.context.scene
try: scene.render.engine = "BLENDER_EEVEE"
except TypeError: scene.render.engine = "BLENDER_EEVEE_NEXT"
scene.view_settings.view_transform = "Standard"; scene.view_settings.look = "None"
scene.render.film_transparent = True
scene.render.image_settings.file_format = "PNG"; scene.render.image_settings.color_mode = "RGBA"
cam_d = bpy.data.cameras.new("c"); cam_d.type = "ORTHO"
cam = bpy.data.objects.new("c", cam_d); scene.collection.objects.link(cam); scene.camera = cam

for j in jobs["jobs"]:
    for o in [o for o in scene.objects if o.type == "MESH"]:
        bpy.data.objects.remove(o)
    W, H = j["canvas"]
    bm = bmesh.new()
    f = bm.faces.new([bm.verts.new((x, -y, -P["depth"])) for x, y in j["points"]])
    ext = bmesh.ops.extrude_face_region(bm, geom=[f])
    bmesh.ops.translate(bm, verts=[g for g in ext["geom"] if isinstance(g, bmesh.types.BMVert)], vec=(0, 0, 2 * P["depth"]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    me = bpy.data.meshes.new("m"); bm.to_mesh(me); bm.free()
    for p in me.polygons: p.use_smooth = True
    ob = bpy.data.objects.new("m", me); scene.collection.objects.link(ob)
    bv = ob.modifiers.new("b", "BEVEL"); bv.width = P["bevel"]; bv.segments = 4
    bv.limit_method = "ANGLE"; bv.angle_limit = math.radians(30); bv.harden_normals = True
    me.materials.append(material(j["tex"], W, H))
    x0, y0, cw, ch = j["crop"]
    cam_d.ortho_scale = max(cw, ch)
    cam.location = (x0 + cw / 2, -(y0 + ch / 2), 200)
    scene.render.resolution_x, scene.render.resolution_y = cw, ch
    scene.render.filepath = j["out"]
    bpy.ops.render.render(write_still=True)
print("HOLDJOBS DONE", len(jobs["jobs"]))
'''


def pad_colours(img, mask):
    solid = img[:, :, 3] > 200
    if not solid.any():
        return img
    _, (iy, ix) = ndimage.distance_transform_edt(~solid, return_indices=True)
    out = img[iy, ix].copy()
    a = img[:, :, 3:4] / 255.0
    out[:, :, :3] = np.where(solid[:, :, None], img[:, :, :3], (img[:, :, :3] * a + out[:, :, :3] * (1 - a)).astype(np.uint8))
    out[:, :, 3] = np.where(ndimage.binary_dilation(mask, iterations=4), 255, 0)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("sheet"); ap.add_argument("cols", type=int); ap.add_argument("rows", type=int)
    ap.add_argument("kind", choices=["body", "cap"]); ap.add_argument("out")
    ap.add_argument("--blender", default="blender")
    ap.add_argument("--depth", type=float, default=10.0)
    ap.add_argument("--bevel", type=float, default=9.0)
    ap.add_argument("--diffuse", type=float, default=1.4)
    ap.add_argument("--spec", type=float, default=0.9)
    a = ap.parse_args()

    sheet = np.array(Image.open(a.sheet).convert("RGBA"))
    fh, fw = sheet.shape[0] // a.rows, sheet.shape[1] // a.cols
    tmp = tempfile.mkdtemp(prefix="holds3d_")
    jobs = []
    for r in range(a.rows):
        for c in range(a.cols):
            fr = sheet[r * fh:(r + 1) * fh, c * fw:(c + 1) * fw]
            if a.kind == "body":   # wrap: seamless when tiled vertically
                canvas = np.concatenate([fr[-PAD:], fr, fr[:PAD]], axis=0)
            else:                  # cap: the body continues above it
                canvas = np.concatenate([np.repeat(fr[:1], PAD, axis=0), fr, np.zeros((PAD, fw, 4), np.uint8)], axis=0)
            mask = canvas[:, :, 3] > 128
            out_png = os.path.join(tmp, "f%d_%d.png" % (r, c))
            if not mask.any():
                Image.fromarray(np.zeros((fh, fw, 4), np.uint8)).save(out_png)
                continue
            tex = os.path.join(tmp, "t%d_%d.png" % (r, c))
            Image.fromarray(pad_colours(canvas, mask)).save(tex)
            cont = max(measure.find_contours(np.pad(mask.astype(float), 1), 0.5), key=len)
            cont = measure.approximate_polygon(cont, 0.6)
            pts = [[float(p[1] - 1), float(p[0] - 1)] for p in cont[:-1]]
            area = 0.5 * sum(pts[i][0] * pts[i - 1][1] - pts[i - 1][0] * pts[i][1] for i in range(len(pts)))
            if area < 0:  # Blender Y is flipped, so keep it counter-clockwise there
                pts = pts[::-1]
            jobs.append({"points": pts, "tex": tex, "canvas": [fw, canvas.shape[0]],
                         "crop": [0, PAD, fw, fh], "out": out_png})
    job_file = os.path.join(tmp, "jobs.json")
    json.dump({"params": {"depth": a.depth, "bevel": a.bevel, "diffuse": a.diffuse, "spec": a.spec}, "jobs": jobs}, open(job_file, "w"))
    script = os.path.join(tmp, "render_holds.py")
    open(script, "w").write(BLENDER_JOB)
    res = subprocess.run([a.blender, "-b", "--factory-startup", "--python", script, "--", job_file],
                         capture_output=True, text=True)
    if "HOLDJOBS DONE" not in res.stdout:
        sys.stderr.write(res.stdout[-3000:] + res.stderr[-3000:]); sys.exit(1)

    out = np.zeros_like(sheet)
    for r in range(a.rows):
        for c in range(a.cols):
            f = np.array(Image.open(os.path.join(tmp, "f%d_%d.png" % (r, c))).convert("RGBA"))
            out[r * fh:(r + 1) * fh, c * fw:(c + 1) * fw] = f
    Image.fromarray(out).save(a.out)
    print("%s -> %s (%d frames rendered)" % (os.path.basename(a.sheet), os.path.basename(a.out), len(jobs)))


if __name__ == "__main__":
    main()
