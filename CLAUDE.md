# CLAUDE.md: Portal VR handoff notes

This document is for the next agent working on this repo. Read it before changing anything. The user-facing description is in README.md.

## What this is

Portal VR is a 6DOF roomscale VR mod for Portal (2007), using OpenVR/SteamVR. The user tests it on a **Steam Frame**; SteamVR reports its `controller_type` as `frame_controller`.

- **Source of the game code:** client.dll and server.dll are rebuilt from Portal-Base (SonicEraZoR). That repo is the leaked Portal 1 code merged into the Source SDK 2013 SP HL2 projects. Its git remote here is `upstream`; our work is on branch `portalvr`, pushed to `origin` (ItsAlphaNeon/Portal-VR-Mod).
- **What it runs on:** the **retail** 32-bit Portal engine, in the user's Steam install `C:\Program Files (x86)\Steam\steamapps\common\Portal`.
  - The retail interfaces are the SDK 2013 SP ones: VClient017, VEngineClient014, ServerGameDLL009.
  - The renderer is shaderapidx9 with D3D9Ex.
- **Mod folder:** `Portal\portalvr` is a junction to `sp/game/portalvr`.
- **Rule: retail files are never modified.**

## User preferences and decisions (confirmed)

- Locomotion is smooth stick plus snap/smooth turn. **No teleport.**
- **Firing:** gun trigger = blue, gun bumper = orange.
- **Hands:** everything interactive is on the **right (gun) controller**.
  - The left hand only walks; it is also used during gun calibration.
- **Picking up:** right-grip *press* toggles pick-up/drop through the gun, held at the barrel like vanilla. Free-hand grabbing was removed at the user's request.
- **Visuals:** no player body. The only visible things are the SteamVR controller models plus the floating gun. Don't show the hand skeleton (it's debug only).
- **Floor/ceiling portal exits:** instant, yaw-only, horizon level by default. `vr_portal_view_mode 1` gives the original rolling view as an option.
- **Gun model:** the RTX Portal gun (user-supplied .blend). Its placement is the user's in-headset calibration, which is baked in as the cvar defaults.
- **Git:** commit or push only when the user asks.
  - They asked once: the "big list" round on 2026-10-03.
  - End commit messages with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- **SteamVR null driver:** may be enabled for desktop testing (`tools/nullhmd.ps1 on`). **Always restore it** (`off`) before the user's headset sessions; `tools/nulltest.ps1` does this automatically.

## Build / deploy / run

- `tools/genprojects.ps1` runs VPC (`/hl2`) and patches the generated vcxproj files for VS2022:
  - v143 toolset
  - warnings are not errors
  - `/permissive /Zc:threadSafeInit-` and related flags
  - `legacy_stdio_definitions`
  - client linker `/FORCE:MULTIPLE`
  - `ml.exe` path fix
  - writes `sp/src/portalvr.sln` with projects `tier1, mathlib, raytrace, vgui_controls, client, server`
- `tools/build.ps1 [-Targets client,server] [-Regenerate]`: runs MSBuild, Release|Win32, and prints errors. Log: `%TEMP%\portalvr_build.log`.
  - Use `-Regenerate` after editing any `.vpc` (for example `client_hl2.vpc`, which lists `vr\*.cpp`).
  - `"client:Rebuild"` forces a clean rebuild.
  - The output goes straight into `sp/game/portalvr/bin`.
- `tools/deploy.ps1`: creates the junction, copies `openvr_api.dll`, and copies the localization files.
- `tools/launch.ps1 [-Flat] [-Map x] [-Extra '+cmd val']`
- `tools/gunmodel/build.ps1`:
  - Runs Blender headless on `build_gun_model.py`, which writes the SMD, QC, VTFs and VMTs.
  - Then runs retail `bin/studiomdl.exe` into `sp/game/portalvr/models/vr/portalgun_rtx.mdl`.
  - The model and materials are **gitignored** (ripped asset). A new checkout must run this script; the asset paths are script parameters.
- `tools/vpkget.py <pak_dir.vpk> <path> [out]` extracts files from retail VPKs. The bundled `vpk.exe` CLI is awkward to use for this.

## Architecture (where things are)

### Client: `sp/src/game/client`

**`vr/vr_openvr.*`: `CPortalVR g_PortalVR`, the OpenVR backend; also implements `ISourceVirtualReality`**
- Startup: `StartRuntime` (needs `-vr`).
- Per frame, `BeginFrame` does:
  - `WaitGetPoses`
  - SteamVR Input
  - hand grip poses
  - raw controller poses and render model names
  - the hand skeleton
  - the debug fake hands and debug headset-pitch override
- **Render targets:**
  - `_rt_vr_eyes` (2W×H) is the shared texture submitted to SteamVR.
  - Each eye renders into its own `_rt_vr_eye_left` / `_rt_vr_eye_right` (W×H).
  - `ResolveEye` copies each eye into its half of `_rt_vr_eyes` (called from `CClientVirtualReality::PostProcessFrame`).
  - The per-eye targets are what fixes glass and refraction: engine copies of the frame buffer must contain exactly one eye.
  - `vr_per_eye_targets 0` restores the old side-by-side path for A/B tests.
- **Other services:**
  - `LoadRenderModel` loads SteamVR render models.
  - `GetGripFromFistSkeleton` returns the grip-limit reference skeleton.
  - Haptics.

**`vr/vr_d3d.*`: D3D side (compiled without the PCH)**
- Finds the engine's D3D9Ex device by scanning shaderapidx9 and comparing vtables against a dummy device.
- Hooks `CreateTexture` (vtable slot 23) so the 2W×H eye texture is created as a shared texture.
- Per frame: event-query fence, then D3D11 `OpenSharedResource` + `CopyResource`, then `Submit` as `TextureType_DirectX`.
- `VRD3D_InstallCrashLogger` is a vectored exception logger. It writes `portalvr/vr_crash.txt` with module+offset return addresses; resolve them against the PDBs in `Release_portalvr`.

**`client_virtualreality.*`: `CClientVirtualReality g_ClientVirtualReality`**
- **Tracking → world:** `world = P + Rz(trackingYaw)*(x − trackingCenter) + (0,0,heightOffset)`, then the optional portal-lerp rotation around the head.
  - `heightOffset` = the seated offset + `m_flDuckJumpOffset`.
  - The duck-jump offset follows the player's view offset when the duck wasn't requested. This hides Source's single-player automatic mid-air duck-jump, which used to make jumps choppy.
- **Roomscale:** `CreateMove` moves `trackingCenter` toward the head (deadzone `vr_roomscale_deadzone`) and sends the same move in `usercmd.vr.roomscaleMove`, which game movement applies with collision.
- **Input mapping:** `CreateMove`. The gun hand drives fire, jump, crouch and `VRBTN_GUN_GRAB`; the left stick only moves.
- **Portal transitions:** `OnLocalPlayerPortalled` (from `C_Portal_Player::DetectAndHandlePortalTeleportation`).
  - Applies a yaw-only turn.
  - With `vr_portal_view_mode 1` it also starts `m_qPortalLerpStart`, which decays over `vr_portal_lerp_time`.
- **Gun:**
  - `m_WorldFromGunModel` = grip pose × (`vr_gun_x/y/z/pitch/yaw/roll`).
  - Aim = model +X rotated by `vr_gun_aim_pitch/yaw`, from the muzzle at model (16.94, 0, 0).
  - The model is a client-only `C_VRGunModel` (a C_BaseAnimating). It overrides `BuildTransformations` to rotate the bones `prong_top`, `prong_left` and `prong_right` about their hinges.
  - `UpdateGunAnimation`:
    - Detects a shot when the weapon's `m_flNextPrimaryAttack` jumps forward.
    - Opens the claws while `IsHoldingObject()`.
    - Applies recoil to the drawn pose only.
    - Sets `$selfillumtint` on the gun material and `$color2` on the glass tube material to the colour of the last portal fired.
  - `DrawGunGlow` draws a sprite in the core.
- **Calibration:** `UpdateGunCalibration`.
  - The free-hand grip carries the gun model.
  - The right stick sets the aim yaw/pitch.
  - A saves `cfg/vr_gun_calibration.cfg` (exec'd from `autoexec.cfg`).
  - B resets to `AutoPlaceGun`, which places the gun from the skeleton.
- **Overlays:** `DrawWorldOverlays` draws the controller render models (static meshes), the debug skeleton, calibration axes, the aim laser and the gun glow. It is called from `CViewRender::DrawViewModels` right after `Push3DView`, so it is depth-tested.
- **Menus/HUD:** `UpdateMenu`, `RenderHUDQuad`, `DrawLaser`.
- **Testing hooks:**
  - `vr_test_cfg` execs a cfg `vr_test_delay` seconds after spawning in each map.
  - `vr_after <sec> <cmd>` is a scheduler. **`wait` does nothing in this engine.**
  - `vr_log_eye` logs the eye position every frame.
  - `vr_debug_turn <deg>`

**`vr/vr_settingspanel.*`: the VR Settings vgui Frame**
- Opened with the `vr_settings` command, from `sp/game/portalvr/resource/gamemenu.res`. That file is a copy of retail's with a "VR SETTINGS" entry added.
- Controls are positioned in `PerformLayout`. Bounds set in the constructor get reset by vgui.

**Stock files touched (search for `Portal VR`)**
- `view.cpp`, `viewrender.cpp`: tracking before CalcView, stereo hooks; no view model in VR; overlay hook.
- `in_main.cpp`, `cdll_client_int.cpp`, `vgui_int.cpp`, `viewpostprocess.cpp` (bloom, AA and colour correction are off in VR).
- `hud_crosshair.cpp`, `portal/hud_quickinfo.cpp`: no reticle in VR.
- `portal/c_portal_player.cpp`: the HMD eye, CalcView, no roll fix-up; the local body isn't drawn in VR.
- `portal/c_weapon_portalgun.cpp`: the world-model gun isn't drawn for the local player in VR.
- `portal/c_prop_portal.cpp` (`Simulate`): adds the VR gun to the portal ghost-renderable list when it reaches into a portal hole. That gives the clip plane on this side and a ghost out of the linked portal.

### Shared: `sp/src/game/shared`

- `vr/vr_usercmd.*`: `VRUserCmd_t` (in `CUserCmd`, serialized in `usercmd.cpp`).
- `portal/portal_gamemovement.cpp`: `RoomscaleMove`.
- `baseplayer_shared.cpp`: lets weapons run while using.

### Server: `sp/src/game/server`

- `portal/portal_player.cpp`:
  - `EyePosition` is the HMD.
  - `GetVRAim` is the muzzle. It falls back to the eye if the muzzle is behind a wall **or the eye→muzzle segment crosses an enabled `trigger_portal_cleanser`**; that is the fizzler fix.
  - `VRProcessGrabButtons`: a gun-grip press injects `IN_USE`.
  - `FindUseEntity`: gun ray, then a 6-unit hull fallback.
  - Precaches the gun model.
  - The old free-hand grab code (`VRFindHandEntity`, `UpdateObjectVRHand`, `vr_grab_radius`, `vr_pull_distance`, `vr_throw_*`) is now unused.
- `portal/weapon_physcannon.cpp`: the grab controller targets the muzzle in gun mode.
- `portal/weapon_portalgun.cpp`: VR aim; `portal_vanilla_gameplay`.

### Mod folder: `sp/game/portalvr`

- `gameinfo.txt`, `cfg/autoexec.cfg` (vanilla-gameplay cvars plus `exec vr_gun_calibration.cfg`).
- `actions/` (manifest plus bindings generated by `tools/gen_bindings.py`).
- `resource/gamemenu.res`.
- Test cfgs `cfg/vrtest_*.cfg`.

## Testing without the headset

1. `tools/nulltest.ps1 -Map <map> -Seconds N -Extra "+vr_test_cfg vrtest_x.cfg"` does the following:
   - Enables the null HMD.
   - **Sacrifices one launch:** SteamVR starts Room Setup for the null HMD, which kills the first game for "not quitting in time". It looks like a crash, but `Steam/logs/vrserver.txt` shows `Kill process ... hl2`.
   - Runs the real test.
   - Kills everything and restores `steamvr.vrsettings`.
2. In the test cfg, use `vr_after`, plus:
   - `vr_dump_eyes <name>`: writes `portalvr/<name>.bmp`, both eyes side by side. Convert with PIL to view it.
   - `vr_debug_fake_hands 1` (+ `vr_debug_fake_hand_yaw/pitch/forward`): gives you a gun without controllers.
   - `vr_debug_hmd_pitch <deg>`: the null HMD otherwise looks wherever it likes.
   - `vr_debug_turn <deg>`
   - `vr_seated 1`: the null HMD sits at floor height, so without it you'd be permanently crouched.
3. Examples:
   - `vrtest_gun.cfg`: gun idle/fire images.
   - `vrtest_glass.cfg`: per-eye A/B on chamber 01's window.
   - `vrtest_jump.cfg`: per-frame eye-height log.
   - `vrtest_menu.cfg`: main menu and VR Settings images.
4. Map notes:
   - `testchmb_a_00` starts with a scripted camera, so the HMD doesn't drive the view.
   - Load `testchmb_a_08` for a map where you have the gun.
5. `tools/smoketest.ps1` loads all 18 campaign maps flat and checks the logs.

## Gotchas

- **Bash tool and backslashes:** the Bash tool may mangle backslashes in heredocs (`\\n` becomes a newline, `\v` becomes a vertical tab).
  - For C++ string literals with escapes, use the Edit/Write tools.
  - Or write Python patch scripts into the scratchpad first.
- **studiomdl rotation:** studiomdl rotates SMD geometry 90° about Z. `build_gun_model.py` pre-rotates (`pre_rot`) the vertices *and* the bone positions.
- **Material vars:** need `$selfillumtint` / `$color2` present in the VMT for `FindVar` to find them.
- **Client-only models:** they need a server precache (`CPortal_Player::Precache`), otherwise `InitializeAsClientEntity` fails.
- **Retail CRT banned functions:** `fopen` / `_snprintf` are banned in client code (`dont_use_fopen` link errors). Use `Q_` functions, Win32 file APIs, or `_snprintf_s`.
- **`Panel.h`:** uses `virtual public` inheritance (`IForceVirtualInheritancePanel`); don't revert that.
- **Archived cvars:** these defaults only take effect in a fresh `config.cfg`. The debug skeleton was moved to the non-archived `vr_debug_skeleton` to drop the old saved value.

## Status (2026-10-03) and next ideas

**Done, verified in the user's headset**
- 6DOF stereo.
- Walking.
- Portals firing.
- Gun calibration: the user's values are baked in as defaults.
- Skeleton-based placement.
- RTX gun model.
- Bigger model; less shine.

**Done this round, verified only with the null HMD and image dumps (not yet in the headset)**
- Gun claw/recoil animation and glow.
- Glass/refraction fix (per-eye targets).
- Smooth jump.
- VR Settings window plus menu entry.
- Seated mode.
- No body, no skeleton.
- Gun-only grabbing.
- Left hand inert.

**Done, not exercised in a test (code review only)**
- Gun ghost through portals.
- Fizzler aim fix.
- `vr_portal_view_mode 1`.

**Ideas and known gaps**
- The gun glow is a sprite plus material tint. The first-person viewmodel effects (beam, particle glow) aren't attached to the new model.
- A one-frame landing dip of about 6 units remains after jumps (client prediction).
- Bloom and colour correction are still off in VR. The effect needs eye-sized buffers.
- Comfort vignette.
- Left-handed mode exists (`vr_gun_hand 0`), but the user only uses the right hand.
