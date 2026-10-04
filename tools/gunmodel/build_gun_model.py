# Portal VR: converts the RTX portal gun (.blend + PNG textures) into Source assets.
#
# Run with Blender (headless):
#   blender -b <gun.blend> --python build_gun_model.py -- <textures dir> <out dir>
#
# Writes into <out dir>:
#   portalgun_rtx.smd / portalgun_rtx.qc            (compile with studiomdl)
#   materials/models/vr/portalgun_rtx/*.vtf/*.vmt
#
# Model space of the result (Source axes, units): +X = barrel forward, +Y = left, +Z = up.
# The origin is on the barrel's center line, under the middle of the gun (roughly where
# a hand would hold it). The muzzle point is printed at the end ("MUZZLE ...").
import bpy, bmesh, numpy as np, os, sys, struct

argv = sys.argv[sys.argv.index("--") + 1:]
TEX_DIR, OUT_DIR = argv[0], argv[1]
UNITS_PER_METER = 39.3701
MODEL_SCALE = 1.5   # the RTX asset is ~2/3 the size of the original gun
TEX_SIZE = 2048

MAT_DIR = os.path.join(OUT_DIR, "materials", "models", "vr", "portalgun_rtx")
os.makedirs(MAT_DIR, exist_ok=True)

# ----------------------------------------------------------------------------- mesh
obj = bpy.data.objects["PortalGun"]
me = obj.data
verts = np.array([v.co[:] for v in me.vertices])
lo, hi = verts.min(0), verts.max(0)

# Mesh local axes: barrel forward = -Y, up = +Z, left = +X.
# Origin: center line in X/Z (from the front section), 30% of the length back from the front.
front = verts[verts[:, 1] < lo[1] + 0.04]
center_x = (front[:, 0].min() + front[:, 0].max()) * 0.5
center_z = (front[:, 2].min() + front[:, 2].max()) * 0.5
origin_y = lo[1] + (hi[1] - lo[1]) * 0.62
ORIGIN = np.array([center_x, origin_y, center_z])

def to_source(p):
    p = np.asarray(p) - ORIGIN
    return np.array([-p[1], p[0], p[2]]) * UNITS_PER_METER * MODEL_SCALE

def dir_to_source(n):
    return np.array([-n[1], n[0], n[2]])

me.calc_loop_triangles()
uv = me.uv_layers.active.data
normals = me.corner_normals
mat_names = ["portalgun_rtx", "portalgun_rtx_glass", "portalgun_rtx_core"]

# ----------------------------------------------------------------------------- prong rig
# The three claws at the front are separate mesh parts arranged around the barrel: top,
# lower left and lower right. Each gets a bone (pivot at its base) so the game can open
# and kick them procedurally (client_virtualreality.cpp, C_VRGunModel).
src_verts = np.array([to_source(v) for v in verts])
bm = bmesh.new(); bm.from_mesh(me); bm.verts.ensure_lookup_table()
comp = np.full(len(bm.verts), -1)
ncomp = 0
for v in bm.verts:
    if comp[v.index] != -1:
        continue
    stack = [v]; comp[v.index] = ncomp
    while stack:
        a = stack.pop()
        for e in a.link_edges:
            b = e.other_vert(a)
            if comp[b.index] == -1:
                comp[b.index] = ncomp; stack.append(b)
    ncomp += 1
bm.free()

PRONGS = [("prong_top", 90.0), ("prong_left", -40.0), ("prong_right", -140.0)]
vert_bone = np.zeros(len(verts), dtype=int)
for c in range(ncomp):
    p = src_verts[comp == c]
    cen = p.mean(0)
    radius = np.hypot(cen[1], cen[2])
    if cen[0] < 6.5 or radius < 3.2:
        continue
    ang = np.degrees(np.arctan2(cen[2], cen[1]))
    best = min(range(3), key=lambda i: abs((ang - PRONGS[i][1] + 180) % 360 - 180))
    if abs((ang - PRONGS[best][1] + 180) % 360 - 180) < 35:
        vert_bone[comp == c] = best + 1

pivots = []
for i, (name, _) in enumerate(PRONGS):
    p = src_verts[vert_bone == i + 1]
    if len(p) == 0:
        raise SystemExit("prong %s not found" % name)
    base = p[p[:, 0] < p[:, 0].min() + 1.0]
    piv = base.mean(0)
    pivots.append(piv)
    print("PRONG %s verts %d pivot %.2f %.2f %.2f tip x %.2f" % (name, len(p), piv[0], piv[1], piv[2], p[:, 0].max()))

# ----------------------------------------------------------------------------- core
# The glass tube (material slot 1) is clear; the thin rod running inside it (a body piece,
# slot 0) is the core, which gets its own material that the game tints with the portal
# color (portalgun_rtx_core, $color2). Tube = the glass piece longest along the barrel;
# core = body pieces inside the tube's cross-section that run at least half its length.
vert_slot = np.zeros(len(verts), dtype=int)
for poly in me.polygons:
    for vi in poly.vertices:
        vert_slot[vi] = poly.material_index
tube = None
for c in np.unique(comp[vert_slot == 1]):
    p = src_verts[comp == c]
    span = p[:, 0].max() - p[:, 0].min()
    if tube is None or span > tube[0]:
        tube = (span, p.min(0), p.max(0))
if tube is None:
    raise SystemExit("glass tube not found")
_, tlo, thi = tube
vert_core = np.zeros(len(verts), dtype=bool)
for c in np.unique(comp[vert_slot == 0]):
    sel = comp == c
    p = src_verts[sel]
    if (p[:, 1].min() > tlo[1] and p[:, 1].max() < thi[1] and p[:, 2].min() > tlo[2] and p[:, 2].max() < thi[2]
            and p[:, 0].min() > tlo[0] - 0.5 and p[:, 0].max() < thi[0] + 1.0
            and p[:, 0].max() - p[:, 0].min() > 0.5 * (thi[0] - tlo[0])):
        vert_core[sel] = True
if not vert_core.any():
    raise SystemExit("core not found")
cc = src_verts[vert_core].mean(0)
print("CORE verts %d center %.2f %.2f %.2f (vr_gun_glow_x/y/z defaults)" % (vert_core.sum(), cc[0], cc[1], cc[2]))

def pre_rot(p):
    # studiomdl turns SMD geometry 90 degrees about Z ((x, y) -> (-y, x)); undo that here.
    return (p[1], -p[0], p[2])

lines = ["version 1", "nodes", '0 "root" -1']
for i, (name, _) in enumerate(PRONGS):
    lines.append('%d "%s" 0' % (i + 1, name))
lines += ["end", "skeleton", "time 0", "0 0 0 0 0 0 0"]
for i, piv in enumerate(pivots):
    q = pre_rot(piv)
    lines.append("%d %.5f %.5f %.5f 0 0 0" % (i + 1, q[0], q[1], q[2]))
lines += ["end", "triangles"]
for tri in me.loop_triangles:
    if tri.material_index == 0 and all(vert_core[me.loops[li].vertex_index] for li in tri.loops):
        lines.append(mat_names[2])
    else:
        lines.append(mat_names[min(tri.material_index, 1)])
    for li in tri.loops:
        vi = me.loops[li].vertex_index
        p = pre_rot(to_source(me.vertices[vi].co))
        n = pre_rot(dir_to_source(normals[li].vector))
        u, v = uv[li].uv
        lines.append("%d %.5f %.5f %.5f %.5f %.5f %.5f %.6f %.6f" % (vert_bone[vi], p[0], p[1], p[2], n[0], n[1], n[2], u, v))
lines.append("end")
with open(os.path.join(OUT_DIR, "portalgun_rtx.smd"), "w") as f:
    f.write("\n".join(lines) + "\n")

with open(os.path.join(OUT_DIR, "portalgun_rtx.qc"), "w") as f:
    f.write('''$modelname "vr/portalgun_rtx.mdl"
$body body "portalgun_rtx.smd"
$cdmaterials "models/vr/portalgun_rtx/"
$surfaceprop "metal"
$illumposition 0 0 0
$sequence idle "portalgun_rtx.smd" fps 1
''')

src_all = np.array([to_source(v) for v in verts])
print("BBOX", src_all.min(0).round(2), src_all.max(0).round(2))
front_src = np.array([to_source(v) for v in front])
print("MUZZLE %.2f 0 0" % src_all[:, 0].max())
print("TRIS", len(me.loop_triangles))

# ----------------------------------------------------------------------------- textures
def load(name, size=TEX_SIZE):
    img = bpy.data.images.load(os.path.join(TEX_DIR, name))
    if img.size[0] != size:
        img.scale(size, size)
    px = np.empty(size * size * 4, np.float32)
    img.pixels.foreach_get(px)
    return px.reshape(size, size, 4)[::-1]  # Blender rows are bottom-up; VTF is top-down

def write_vtf(path, rgba, flags):
    # VTF 7.2, BGRA8888, full mip chain, no low-res thumbnail.
    h, w = rgba.shape[:2]
    mips = [rgba]
    while mips[-1].shape[0] > 1 and mips[-1].shape[1] > 1:
        m = mips[-1]
        mips.append((m[0::2, 0::2] + m[1::2, 0::2] + m[0::2, 1::2] + m[1::2, 1::2]) * 0.25)
    refl = rgba[..., :3].reshape(-1, 3).mean(0)
    hdr = struct.pack("<4sIIIHHIHH4s3f4sfIBIBBH", b"VTF\0", 7, 2, 80, w, h, flags, 1, 0, b"\0" * 4,
                      refl[0], refl[1], refl[2], b"\0" * 4, 1.0, 12, len(mips), 0xFFFFFFFF, 0, 0, 1)
    hdr = hdr.ljust(80, b"\0")
    with open(path, "wb") as f:
        f.write(hdr)
        for m in reversed(mips):  # smallest first
            b = np.clip(m * 255.0 + 0.5, 0, 255).astype(np.uint8)
            f.write(b[..., [2, 1, 0, 3]].tobytes())

TEXTUREFLAGS_NORMAL = 0x80
TEXTUREFLAGS_EIGHTBITALPHA = 0x2000

albedo = load("Albedoout.tga.png")
emissive = load("T_Charracter_PortalGun_Emissiveout.tga.png")
albedo[..., 3] = np.clip(emissive[..., :3].max(-1), 0, 1)  # self-illum mask
write_vtf(os.path.join(MAT_DIR, "albedo.vtf"), albedo, TEXTUREFLAGS_EIGHTBITALPHA)
del albedo, emissive

normal = load("T_Charracter_PortalGun_Normal_OTHout.png")
metal = load("T_Charracter_PortalGun_Metalout.tga.png")[..., 0]
rough = load("T_Charracter_PortalGun_Roughout.tga.png")[..., 0]
normal[..., 3] = np.clip(0.05 + metal * 0.35 + (1.0 - rough) * 0.25, 0, 1)  # phong / envmap mask
write_vtf(os.path.join(MAT_DIR, "normal.vtf"), normal, TEXTUREFLAGS_NORMAL | TEXTUREFLAGS_EIGHTBITALPHA)
del normal

glass = np.zeros((64, 64, 4), np.float32)
glass[...] = (0.45, 0.45, 0.45, 1.0)
write_vtf(os.path.join(MAT_DIR, "glass.vtf"), glass, TEXTUREFLAGS_EIGHTBITALPHA)
core = np.ones((64, 64, 4), np.float32)
write_vtf(os.path.join(MAT_DIR, "core.vtf"), core, TEXTUREFLAGS_EIGHTBITALPHA)

with open(os.path.join(MAT_DIR, "portalgun_rtx.vmt"), "w") as f:
    f.write('''"VertexLitGeneric"
{
	"$basetexture"	"models/vr/portalgun_rtx/albedo"
	"$bumpmap"		"models/vr/portalgun_rtx/normal"
	"$selfillum"	"1"
	"$selfillumtint"	"[1 1 1]"
	"$phong"		"1"
	"$phongexponent"	"12"
	"$phongboost"	"0.6"
	"$phongfresnelranges"	"[0.4 0.8 1]"
	"$envmap"		"env_cubemap"
	"$normalmapalphaenvmapmask"	"1"
	"$envmaptint"	"[0.06 0.06 0.06]"
}
''')
# Clear glass tube: a faint additive sheen plus cubemap reflections, no tint.
with open(os.path.join(MAT_DIR, "portalgun_rtx_glass.vmt"), "w") as f:
    f.write('''"UnlitGeneric"
{
	"$basetexture"	"models/vr/portalgun_rtx/glass"
	"$additive"		"1"
	"$nocull"		"1"
	"$color2"		"[0.08 0.08 0.08]"
	"$envmap"		"env_cubemap"
	"$envmaptint"	"[0.25 0.25 0.25]"
}
''')
# The core inside the tube: fully emissive, in the color of the last portal (the game sets
# $color2 every frame; keep the key so FindVar finds it).
with open(os.path.join(MAT_DIR, "portalgun_rtx_core.vmt"), "w") as f:
    f.write('''"UnlitGeneric"
{
	"$basetexture"	"models/vr/portalgun_rtx/core"
	"$color2"		"[0.25 0.6 1]"
	"$model"		"1"
}
''')
print("DONE")
