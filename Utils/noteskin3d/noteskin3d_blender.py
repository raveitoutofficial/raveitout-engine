"""Build a beveled 3D tap note in Blender and export it for the engine.

Run headless (after tools/noteskin3d_prep.py):
  blender -b --factory-startup --python tools/noteskin3d_blender.py -- \
      DIR NAME [--depth 5] [--bevel 2.5] [--segments 3] [--render PREVIEW.png]

Reads  DIR/_NAME outline.json  (from the prep step)
Writes DIR/_NAME model.txt     Milkshape 3D ASCII: meshes + material + bones
       PREVIEW.png             (optional) 4 views rendered with EEVEE

Model space: 2 texture pixels = 1 unit, Y down (engine convention), the
note is centred on 0 and spans -depth..+depth in Z. The art is mapped
straight onto X/Y, so the bevel wraps the art's edge over the rim. The
material adds "_shine sphere.png" as a sphere-mapped additive layer, which
the engine turns into a moving highlight on the beveled edges.
"""
import bpy, bmesh, json, math, os, sys

argv = sys.argv[sys.argv.index("--") + 1:]
DIR, NAME = argv[0], argv[1]
def opt(flag, default):
    return type(default)(argv[argv.index(flag) + 1]) if flag in argv else default
DEPTH = opt("--depth", 8.0)
BEVEL = opt("--bevel", 2.5)
SEGMENTS = opt("--segments", 3)
RENDER = opt("--render", "")
LIT = "--lit" in argv   # real engine lighting: plain walls + shiny materials

bpy.ops.wm.read_factory_settings(use_empty=True)
data = json.load(open(os.path.join(DIR, "_%s outline.json" % NAME)))
W, H = data["width"], data["height"]
pts = [((x - W / 2) / 2.0, (y - H / 2) / 2.0) for x, y in data["points"]]

# --- geometry: outline -> extruded solid -> bevel -> triangles -------------
bm = bmesh.new()
face = bm.faces.new([bm.verts.new((x, y, -DEPTH)) for x, y in pts])
ext = bmesh.ops.extrude_face_region(bm, geom=[face])
top = [g for g in ext["geom"] if isinstance(g, bmesh.types.BMVert)]
bmesh.ops.translate(bm, verts=top, vec=(0, 0, 2 * DEPTH))
bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
me = bpy.data.meshes.new(NAME)
bm.to_mesh(me); bm.free()
for p in me.polygons:
    p.use_smooth = True
obj = bpy.data.objects.new(NAME, me)
bpy.context.scene.collection.objects.link(obj)

bev = obj.modifiers.new("bevel", "BEVEL")
bev.width = BEVEL
bev.segments = SEGMENTS
bev.limit_method = "ANGLE"
bev.angle_limit = math.radians(30)
bev.profile = 0.5
bev.harden_normals = True           # flat faces stay flat, rounded rims shade smoothly
tri = obj.modifiers.new("tri", "TRIANGULATE")
tri.keep_custom_normals = True

dg = bpy.context.evaluated_depsgraph_get()
final = bpy.data.meshes.new_from_object(obj.evaluated_get(dg), preserve_all_data_layers=True, depsgraph=dg)
final.calc_loop_triangles()

# --- export Milkshape ASCII ------------------------------------------------
def uv_of(co):
    return ((co.x * 2 + W / 2) / W, (co.y * 2 + H / 2) / H)

normals, nidx = [], {}
def normal_index(n):
    key = (round(n.x, 4), round(n.y, 4), round(n.z, 4))
    if key not in nidx:
        nidx[key] = len(normals); normals.append(key)
    return nidx[key]

cn = final.corner_normals
# Group triangles: 0 = front/back faces (the art), 1..3 = side walls and
# bevels shaded light / mid / dark by which way they face (light from the
# upper left, Y down). The engine doesn't light notes, so this is what makes
# the thickness visible when notes turn (Twirl, Roll, perspective mods).
LX, LY = -0.7071, -0.7071
groups = {0: [], 1: [], 2: [], 3: [], 4: []}
for lt in final.loop_triangles:
    v = [final.loops[l].vertex_index for l in lt.loops]
    n = [normal_index(cn[l].vector) for l in lt.loops]
    fn = lt.normal
    if abs(fn.z) > 0.9:
        g = 0
    else:
        d = (fn.x * LX + fn.y * LY) / max(1e-6, (fn.x ** 2 + fn.y ** 2) ** 0.5)
        g = 1 if d > 0.35 else (3 if d < -0.35 else 2)
        if LIT:
            g = 4   # one plain wall colour; the engine's light does the shading
    groups[g].append((v[0], v[1], v[2], n[0], n[1], n[2]))

mesh_lines = []
used = [g for g in (0, 1, 2, 3, 4) if groups[g]]
for mi, g in enumerate(used):
    mesh_lines += ['"part%d" 0 %d' % (g, mi), str(len(final.vertices))]
    for vt in final.vertices:
        u, w = uv_of(vt.co)
        # The engine's 3D models are Y-UP (2D sprites are Y-down), so mirror Y on
        # export; UVs come from the un-mirrored position, so the art isn't flipped.
        mesh_lines.append("0 %.5f %.5f %.5f %.5f %.5f -1" % (vt.co.x, -vt.co.y, vt.co.z, u, w))
    mesh_lines.append(str(len(normals)))
    mesh_lines += ["%.5f %.5f %.5f" % (nn[0], -nn[1], nn[2]) for nn in normals]
    mesh_lines.append(str(len(groups[g])))
    # Mirroring Y reverses winding; swap two corners to keep faces front-facing.
    mesh_lines += ["0 %d %d %d %d %d %d 1" % (t[0], t[2], t[1], t[3], t[5], t[4]) for t in groups[g]]

lines = ["// MilkShape 3D ASCII", "", "Frames: 1", "Frame: 1", "",
         "Meshes: %d" % len(used)] + mesh_lines + ["", "Materials: %d" % len(used)]
for g in used:
    tex = '"_%s tex.ini"' % NAME if g == 0 else '"_%s rim %d.png"' % (NAME, g - 1)
    spec, shine = ("0.9 0.9 0.9 1.0", "40.0") if LIT else ("0.0 0.0 0.0 1.0", "0.0")
    lines += ['"m%d"' % g, "1.0 1.0 1.0 1.0", "1.0 1.0 1.0 1.0", spec, "0.0 0.0 0.0 1.0",
              shine, "1.0", tex, '"_shine sphere.png"']
lines += ["", "Bones: 0", "GroupComments: 0", "MaterialComments: 0", "BoneComments: 0", "ModelComment: 0", ""]
with open(os.path.join(DIR, "_%s model.txt" % NAME), "w", newline="\r\n") as f:
    f.write("\n".join(lines))
tris = sum(len(groups[g]) for g in used)
print("EXPORT %s: %d vertices, %d triangles, %d parts" % (NAME, len(final.vertices), tris, len(used)))

# --- optional preview render ----------------------------------------------
if RENDER:
    scene = bpy.context.scene
    view = bpy.data.objects.new("view", final)
    scene.collection.objects.link(view)
    obj.hide_render = True

    mat = bpy.data.materials.new("art"); mat.use_nodes = True
    nt = mat.node_tree; bsdf = nt.nodes["Principled BSDF"]
    img = nt.nodes.new("ShaderNodeTexImage")
    img.image = bpy.data.images.load(os.path.join(DIR, "_%s frame 0.png" % NAME))
    tc = nt.nodes.new("ShaderNodeTexCoord"); mp = nt.nodes.new("ShaderNodeMapping")
    mp.inputs["Location"].default_value = (0.5, 0.5, 0)
    mp.inputs["Scale"].default_value = (2.0 / W, -2.0 / H, 1)
    nt.links.new(tc.outputs["Object"], mp.inputs["Vector"])
    nt.links.new(mp.outputs["Vector"], img.inputs["Vector"])
    nt.links.new(img.outputs["Color"], bsdf.inputs["Base Color"])
    bsdf.inputs["Metallic"].default_value = 0.35
    bsdf.inputs["Roughness"].default_value = 0.3
    view.data.materials.append(mat)

    world = bpy.data.worlds.new("w"); scene.world = world
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs["Color"].default_value = (0.02, 0.02, 0.03, 1)
    world.node_tree.nodes["Background"].inputs["Strength"].default_value = 1.0
    for loc, energy in (((-60, -80, -120), 4.0), ((80, 40, -60), 1.5)):
        ld = bpy.data.lights.new("sun", "SUN"); ld.energy = energy
        lo = bpy.data.objects.new("sun", ld); lo.location = loc
        scene.collection.objects.link(lo)
        c = lo.constraints.new("TRACK_TO"); c.target = view

    cam_d = bpy.data.cameras.new("cam"); cam_d.type = "ORTHO"; cam_d.ortho_scale = 85
    cam = bpy.data.objects.new("cam", cam_d); scene.collection.objects.link(cam); scene.camera = cam
    cam.location = (0, 0, -200); cam.rotation_euler = (math.pi, 0, 0)   # look +Z, X right, Y down
    try:
        scene.render.engine = "BLENDER_EEVEE"
    except TypeError:
        scene.render.engine = "BLENDER_EEVEE_NEXT"
    scene.render.resolution_x = scene.render.resolution_y = 360
    scene.render.film_transparent = False
    base = os.path.splitext(RENDER)[0]
    for i, (rx, ry) in enumerate(((0, 0), (25, -35), (0, -70), (-30, 20))):
        view.rotation_euler = (math.radians(rx), math.radians(ry), 0)
        scene.render.filepath = "%s_view%d.png" % (base, i)
        bpy.ops.render.render(write_still=True)
    print("RENDERED %s_view0..3.png" % base)
