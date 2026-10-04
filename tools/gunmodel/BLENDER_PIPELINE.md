# Gun model pipeline: headless Blender → Source

This guide explains how the hand-held portal gun (`models/vr/portalgun_rtx.mdl`) is made from the Portal RTX gun asset. It's written for an agent that has to change the model: its size, origin, claws, materials or textures.

Nobody ever opens the Blender UI. Every change is made in `build_gun_model.py`, a Python script that Blender runs headless (`-b`). The output is plain text (SMD, QC, VMT) and binary VTFs. Retail `studiomdl.exe` then compiles those into the `.mdl`.

The **compiled output is committed** to `sp/game/portalvr/models/vr/` and `sp/game/portalvr/materials/models/vr/portalgun_rtx/`. After a change, rebuild and commit those files too.

---

## 1. Inputs and tools

| What | Default path (a parameter of `build.ps1`) |
|---|---|
| Source `.blend` (Sketchfab export of the RTX gun) | `C:\Users\Neon\Downloads\portal-rtx-assets-portal-gun\source\Sketchfab_2023_05_08_17_26_35.blend` |
| PNG textures | `...\portal-rtx-assets-portal-gun\textures\` |
| Blender | `C:\Program Files\Blender Foundation\Blender 5.1\blender.exe` (5.x; the script uses the 4.1+ `me.corner_normals` API) |
| studiomdl | `<Portal>\bin\studiomdl.exe` (retail, the one shipped with the game) |

Texture files used (exact names):
- `Albedoout.tga.png` (colour)
- `T_Charracter_PortalGun_Emissiveout.tga.png` (glow mask)
- `T_Charracter_PortalGun_Normal_OTHout.png` (normal map)
- `T_Charracter_PortalGun_Metalout.tga.png`
- `T_Charracter_PortalGun_Roughout.tga.png`

The `.blend` has a single mesh object named **`PortalGun`**. It uses two material slots: index 0 is the body and index 1 is the glass tube.

## 2. Running it

```powershell
.\tools\gunmodel\build.ps1          # optional: -Blend, -Textures, -Blender, -Portal
```

`build.ps1` does three things:

1. `blender -b --factory-startup <blend> --python build_gun_model.py -- <textures> tools/gunmodel/build`
   - `-b` runs Blender headless.
   - `--factory-startup` ignores user prefs and add-ons, so runs are reproducible.
   - Everything after `--` is the script's own arguments, read from `sys.argv[sys.argv.index("--")+1:]`.
   - The script's log lines are filtered to `BBOX|MUZZLE|TRIS|PRONG|DONE|Error`, so those are the only ones you see.
2. Copies `build/materials/*` into `sp/game/portalvr/materials`.
3. Runs `studiomdl.exe -nop4 -game sp/game/portalvr portalgun_rtx.qc` from `build/`. The `.mdl/.vvd/.vtx` files go to `sp/game/portalvr/models/vr/`.

`tools/gunmodel/build/` is scratch output and is gitignored.

**Check the output:**
- `PRONG <name> verts N pivot x y z tip x`: three lines, one per claw, each with a sensible vertex count. If a claw isn't found, the script exits with `prong X not found`.
- `BBOX`: the model's bounding box in game units. Source X is barrel forward.
- `MUZZLE x 0 0`: the barrel tip. **If this changes, update `s_vecGunMuzzleInModel` in `sp/src/game/client/client_virtualreality.cpp:91`** (currently `16.94`).
- `TRIS`, then `DONE`.
- studiomdl's last line should mention `portalgun_rtx.mdl` and have no `ERROR`.

To see it in game, use `tools/nulltest.ps1` with `vrtest_gun.cfg`, which dumps idle and fire images (see CLAUDE.md, "Testing without the headset"). You can also just launch the game: the cvar `vr_gun_model` points at the model.

## 3. What the script does, step by step

### 3.1 Axes, origin and scale
- The mesh's local axes are: barrel forward = **−Y**, left = **+X**, up = **+Z**, in metres.
- Source model space is: **+X forward, +Y left, +Z up**, in game units.
- `to_source(p)` subtracts `ORIGIN`, then maps `(x, y, z) → (−y, x, z)`, then multiplies by `39.3701 * MODEL_SCALE`. `dir_to_source` does the same mapping for normals, with no scale.
- `ORIGIN` is chosen like this:
  - X/Z: the centre of the vertices within 4 cm of the front, i.e. the barrel's centre line.
  - Y: 62% of the length back from the front, roughly where the hand holds the gun.
- `MODEL_SCALE = 1.5`. The user asked for the gun 1.5× bigger. The RTX asset is only about 2/3 the size of the original gun.

**Moving the origin or changing the scale shifts the gun in the hand.** The user's calibration (`vr_gun_x/y/z/pitch/yaw/roll`, baked in as cvar defaults in `client_virtualreality.cpp`) is relative to this origin. Avoid changing it. If you must, tell the user to recalibrate (VR Settings → Calibrate gun position), or compensate the defaults. Use `vr_gun_scale` for runtime size tests instead.

### 3.2 Claw ("prong") rig
The game animates three claws (they open while holding an object and flick open on each shot). To make that possible, the script gives each claw its own bone:

1. **Find the separate pieces.** A BFS over `bmesh` edges labels each connected piece of the mesh (`comp[]`). The claws are separate pieces in the asset.
2. **Pick out the claws.** A piece counts as a claw if:
   - its centre is in front of x = 6.5 units,
   - it's more than 3.2 units from the barrel axis,
   - and its angle around the barrel (`atan2(z, y)`) is within 35° of one of the claw angles below.

   | Bone | Angle around barrel |
   |---|---|
   | `prong_top` | 90° |
   | `prong_left` | −40° |
   | `prong_right` | −140° |

   The thresholds are in Source units after scaling. **If you change `MODEL_SCALE`, retune 6.5 / 3.2** and check the `PRONG` lines.
3. **Pivots.** A claw's pivot is the average of its vertices within 1 unit of its rearmost point, which is the hinge.
4. **Skeleton.** Bone 0 is `root`. Bones 1–3 are the claws, each parented to `root` and positioned at its pivot with no rotation. Each vertex is weighted 100% to one bone (the SMD's first number on each vertex line).

The client finds the bones **by name**, in `C_VRGunModel` in `client_virtualreality.cpp` (around line 760). It overrides `BuildTransformations` and rotates each claw bone about its hinge. Bone names are part of that contract; if you rename them, change `s_pszProngBones` in the client too.

### 3.3 The studiomdl 90° gotcha
studiomdl rotates SMD geometry 90° about Z on import: `(x, y) → (−y, x)`. Left as is, the barrel points along +Y in game.

`pre_rot(p) = (y, −x, z)` undoes that in advance. It's applied to **vertex positions, normals and bone positions**. If you forget it on the bones, the claws hinge around points that are rotated off to the side. Don't try to fix this with `$upaxis` or a `$sequence rotate` instead: both were less predictable than pre-rotating.

### 3.4 Writing the SMD and QC
- The SMD is written by hand (no exporter add-on needed):
  - the `nodes` list,
  - one `skeleton` frame (`time 0`),
  - `triangles`: for each `me.loop_triangles` triangle, the material name, then three lines of `bone px py pz nx ny nz u v`.
- Normals come from `me.corner_normals`, so split and custom normals survive. UVs come from the active UV layer.
- Material name: `portalgun_rtx` for slot 0, `portalgun_rtx_glass` for slot 1 and up.
- The QC is minimal:
  - `$modelname vr/portalgun_rtx.mdl`
  - `$cdmaterials models/vr/portalgun_rtx/`
  - `$surfaceprop metal`
  - `$illumposition 0 0 0`
  - an `idle` sequence that reuses the reference SMD.

  There's no collision model: the gun is a client-only visual. It still needs the server precache in `CPortal_Player::Precache`; without it, `InitializeAsClientEntity` fails.

### 3.5 Textures: a VTF writer written by hand
There's no VTFLib/VTFEdit dependency.
- `load()` loads PNGs with `bpy.data.images.load`, resizes to 2048² with `img.scale`, reads pixels with `foreach_get`, and **flips rows**: Blender is bottom-up, VTF is top-down.
- `write_vtf()` writes **VTF 7.2, BGRA8888, uncompressed**:
  - an 80-byte header; format id 12 = BGRA8888; reflectivity = the average colour;
  - no low-res thumbnail (`0xFFFFFFFF`);
  - a full mip chain made by 2×2 box filtering, written **smallest mip first**.
- Flags: `0x2000` (8-bit alpha) everywhere, plus `0x80` (normal map) on the normal map.

These files are big (2048² × 4 bytes × 4/3 ≈ 21 MB each). If size becomes a problem, add DXT5 compression or drop to 1024².

Channel packing (Source's VertexLitGeneric conventions):

| VTF | RGB | Alpha |
|---|---|---|
| `albedo.vtf` | albedo | **self-illum mask** = max(emissive RGB) |
| `normal.vtf` | normal (OpenGL-style "OTH" normal from the asset, used as is) | **phong/envmap mask** = 0.05 + 0.35·metal + 0.25·(1−rough) |
| `glass.vtf` | flat 0.45 grey, 64² | 1 |

### 3.6 Materials (VMT)
- **`portalgun_rtx.vmt`** (`VertexLitGeneric`):
  - `$selfillum 1` with **`$selfillumtint`**
  - phong: exponent 12, boost 0.6
  - `env_cubemap` with `$normalmapalphaenvmapmask` and a dim `$envmaptint 0.06`. It was turned down because the user said the gun was too shiny.
- **`portalgun_rtx_glass.vmt`** (`UnlitGeneric`): `$additive`, `$nocull`, **`$color2`**.

The game writes `$selfillumtint` and `$color2` every frame with the last portal's colour (`UpdateGunAnimation`, `client_virtualreality.cpp` around line 900). It uses `FindVar`, which only finds variables **present in the VMT**. Keep those keys if you rewrite the materials, and keep the material paths, which are hard-coded in `FindMaterial`.

## 4. Recipes

- **Change the size:** edit `MODEL_SCALE`, retune the claw thresholds (§3.2), rebuild, update the muzzle constant from `MUZZLE`, and have the user recalibrate (§3.1).
- **Tweak shine or glow:** edit the VMT strings at the bottom of the script. Or, for a quick test, edit the copied `.vmt` in `sp/game/portalvr/materials/...` directly and `mat_reloadallmaterials`. Then put the same change in the script so the next rebuild keeps it.
- **Add a moving part:** split it off like the claws: find its piece, assign `vert_bone`, add a node and a skeleton line (with `pre_rot`), then drive it by name in `C_VRGunModel::BuildTransformations`.
- **Different source asset:** change the object name (`bpy.data.objects["PortalGun"]`), the axis mapping in `to_source`/`dir_to_source`, the `ORIGIN` rule and the texture file names. Print the bounding box (`lo`, `hi`) first to work out the axes.
- **Debugging inside Blender:** `print()` goes to stdout. Run the `blender -b ...` line without the `Select-String` filter to see everything, including Python tracebacks.

## 5. Checklist before committing
1. `build.ps1` shows three `PRONG` lines, `MUZZLE`, `DONE`, and studiomdl reports no errors.
2. The muzzle constant in the client matches `MUZZLE`, and the client is rebuilt if it changed.
3. The gun looks right in an image dump or in the headset: it sits in the hand, the claws hinge correctly, and the glow changes colour on fire.
4. Commit `tools/gunmodel/*`, `sp/game/portalvr/models/vr/*` and `sp/game/portalvr/materials/models/vr/portalgun_rtx/*`.
