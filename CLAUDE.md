# CLAUDE.md: Portal VR handoff notes

This document is for the next agent working on this repo. Read it before changing anything. The user-facing description is in README.md.

## What this is

Portal VR is a 6DOF roomscale VR mod for Portal (2007), using OpenVR/SteamVR. The user tests it on a **Steam Frame**; SteamVR reports its `controller_type` as `frame_controller`.

- **Source of the game code:** client.dll and server.dll are rebuilt from Portal-Base (SonicEraZoR). That repo is the leaked Portal 1 code merged into the Source SDK 2013 SP HL2 projects. Our work is on branch `portalvr`, pushed to `origin` (ItsAlphaNeon/Portal-VR-Mod).
- **The repo is pruned (2026-10-06).** Only what the Windows client/server build reads is kept. The list came from MSBuild's `*.read.*.tlog` files after a full build, plus the VPC inputs, which those logs don't show (`vpc_scripts`, the `*.vpc` files, `devtools/bin/vpc.exe`).
  - Removed: all of `sp/game` except `portalvr` (the HL2/episodic/ep2/lostcoast content, the `*_with_ashpd` mods, `model_src`), `mp/`, `gcsdk`, `thirdparty/protobuf-2.3.0`, `materialsystem`, `dx9sdk`/`dx10sdk`, `fgdlib`, the map tools in `utils`, non-Windows `devtools/bin`, the createallprojects scripts, and every unused `lib/` file (osx32/linux32/2010/2012, protobuf, matsys_controls, particles, vmpi...).
  - The history was rewritten with `git filter-repo` (510 MB → 64 MB clone). Upstream Portal-Base can no longer be merged directly: port its changes by hand. Pre-prune backup: `../Portal-VR-Mod-backup-2026-10-06.bundle`.
  - If a build fails on a missing header or lib, take it from Portal-Base or the backup bundle and commit only that file. Don't re-add whole folders.
- **Built binaries stay committed** for convenience:
  - `sp/game/portalvr/bin/client.dll` and `server.dll`: the mod.
  - `sp/src/lib/public/*.lib`: Valve's prebuilt libs the link needs.
  - `tier1`, `mathlib`, `raytrace` and `vgui_controls.lib` are gitignored intermediates that every build regenerates (`vgui_controls.lib` is 36 MB); keep them out of git so the history doesn't grow again.
  - Commit the DLLs with every code change you ship.
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
  - **Without the portal gun** (first chambers) the right hand grabs what it touches or points at (calibrated aim + 6-unit sweep), and the object floats in front like with the gun at `vr_hand_hold_scale` (0.8) of the distance. Holding it in the hand bumped the player collider and dropped it.
- **Visuals:** no player body. The only visible things are the SteamVR controller models plus the floating gun. Don't show the hand skeleton (it's debug only).
- **Floor/ceiling portal exits:** instant, yaw-only, horizon level by default. `vr_portal_view_mode 1` gives the original rolling view as an option.
- **Gun model:** the RTX Portal gun (user-supplied .blend). Its placement is the user's in-headset calibration, which is baked in as the cvar defaults.
- **Git:** commit or push only when the user asks.
  - They asked once: the "big list" round on 2026-10-03.
  - End commit messages with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- **SteamVR null driver:** may be enabled for desktop testing (`tools/nullhmd.ps1 on`). **Always restore it** (`off`) before the user's headset sessions; `tools/nulltest.ps1` does this automatically.

## Build / deploy / run

- **Player entry point:** `launch.bat` → `tools/play.ps1`: finds Portal through Steam (`tools/findportal.ps1`, also used by deploy/launch), builds only if `bin/client.dll` is missing, runs deploy, then launch. **The prebuilt `client.dll` / `server.dll` in `sp/game/portalvr/bin` are committed** so a friend can play without Visual Studio: rebuild and commit them with every code change you ship.
- `tools/genprojects.ps1` runs VPC (`/hl2`) and patches the generated vcxproj files:
  - the newest installed toolset (v143 = VS2022, v145 = VS2026; MSBuild is found with vswhere)
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
- `tools/launch.ps1 [-Flat] [-Map x] [-Extra '+cmd val'] [-Console]`
- **Sending commands to a running game:** `hl2.exe -game portalvr -hijack +cmd arg1 arg2` (each argument a separate token). `jpeg` saves a screenshot to `portalvr/screenshots` (works in flat mode, where `vr_dump_eyes` doesn't).
- `tools/gunmodel/build.ps1` (full guide: `tools/gunmodel/BLENDER_PIPELINE.md`):
  - Runs Blender headless on `build_gun_model.py`, which writes the SMD, QC, VTFs and VMTs.
  - Then runs retail `bin/studiomdl.exe` into `sp/game/portalvr/models/vr/portalgun_rtx.mdl`.
  - The converted model and materials are **committed** (`models/vr/`, `materials/models/vr/`), so a new checkout works without Blender. Re-run this script and commit the output after changing the model; the source asset paths are script parameters.
  - Step-by-step guide to the headless Blender pipeline: `tools/gunmodel/BLENDER_PIPELINE.md`.
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
    - Sets `$selfillumtint` on the gun material and `$color2` on the core material (`portalgun_rtx_core`, the rod inside the clear glass tube) to the colour of the last portal fired.
  - `DrawGunGlow` draws a sprite at `vr_gun_glow_x/y/z` (model space), size `vr_gun_glow_size`. The defaults are the user's in-headset tuning.
  - **Grab electricity:** `UpdateGunBeams` draws the three lightning beams (claws → front of the barrel) while holding an object. The stock ones hang off the hidden view model, so `C_WeaponPortalgun::DoEffectHolding` keeps those off for the local VR player. Start = claw hinge + `vr_gun_beam_claw1/2/3` ("x y z" offset in model axes), swung with the claw animation; end = `vr_gun_beam_end_x/y/z`.
  - **Grab electricity editor:** `vr_gun_beam_edit` (VR Settings → Edit grab electricity). Right grip click cycles the point (barrel end, top/left/right claw; the selected one shows axes, the others white crosses), left stick moves forward/left, right stick up/down, speed `vr_gun_beam_edit_speed` (10/s), A saves `cfg/vr_gun_beam.cfg` (exec'd from `autoexec.cfg`), B resets.
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
- `portal/c_weapon_portalgun.cpp`: the world-model gun isn't drawn for the local player in VR, and its view-model grab beams stay off.
- **Screen-only mode** (`m_bScreenOnly`): end credits (`portal_credits.cpp` `g_bPortalRollingCredits`) and the title screen (`engine->IsLevelMainMenuBackground()`). Each eye is cleared to black, the controllers are redrawn, and the menu screen is drawn straight ahead. `OverrideView` uses the tracked head instead of the scripted camera. On the title screen, `CViewRender::DrawVRTitleScene` (called from `view.cpp` once per frame) renders the flyby camera flat into `_rt_vr_title` (created in `vr_openvr.cpp`), and the screen shows it under the menu.
- `portal/c_prop_portal.cpp` (`Simulate`): adds the VR gun to the portal ghost-renderable list when it reaches into a portal hole. That gives the clip plane on this side and a ghost out of the linked portal.

### Shared: `sp/src/game/shared`

- `vr/vr_usercmd.*`: `VRUserCmd_t` (in `CUserCmd`, serialized in `usercmd.cpp`).
- `portal/portal_gamemovement.cpp`: `RoomscaleMove`.
- `baseplayer_shared.cpp`: lets weapons run while using.

### Server: `sp/src/game/server`

- `portal/portal_player.cpp`:
  - `EyePosition` is the HMD.
  - `GetVRAim` is the muzzle. It falls back to the eye if the muzzle is behind a wall **or the eye→muzzle segment crosses an enabled `trigger_portal_cleanser`**; that is the fizzler fix.
  - `VRProcessGrabButtons`: a gun-grip press injects `IN_USE`; mode GUN with the portal gun, HAND without.
  - `FindUseEntity`: GUN = gun ray, then a 6-unit hull fallback. HAND = `VRFindHandEntity` on the gun hand (touch radius `vr_grab_radius` 14, then ray + hull along the calibrated aim).
  - `vr_grab_log 1` logs every grab: what was near/hit, `PickupObject` refusals (standing on it, too heavy) and why the pickup controller let go.
  - **`max_lift_mass` is 85** (Portal's value; code default + autoexec). HL2's 35 can't lift the 40 kg cube without a weapon.
  - Precaches the gun model.
  - Unused leftovers of the old free-hand grab: `vr_throw_*`, `m_matVRHandFromObject`.
- `portal/weapon_physcannon.cpp`: the grab controller holds objects along the VR aim from the muzzle (GUN and HAND modes; HAND is scaled by `vr_hand_hold_scale`).
- `portal/weapon_portalgun.cpp`: VR aim; `portal_vanilla_gameplay`.
- `portal/portal_player.cpp` `PostThink`: feeds the nerve gas countdown (`startneurotoxins`, escape_02) into `SetBonusProgress`, which the `vgui_neurotoxin_countdown` screens display. That code was missing, so the timer read 00:00:00.

### Mod folder: `sp/game/portalvr`

- `gameinfo.txt`, `cfg/autoexec.cfg` (vanilla-gameplay cvars plus `exec vr_gun_calibration.cfg`).
- `actions/` (manifest plus bindings generated by `tools/gen_bindings.py`): Frame, Index (`knuckles`), Touch, Cosmos, Vive wands, WMR, Reverb G2. Orange portal is the bumper only where one exists (Frame, Cosmos).
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

**Done 2026-10-04**
- Verified in the headset: default gun pose = the user's calibration; clear glass tube with an emissive portal-coloured core; tuned core glow; grab electricity on the VR gun (tuned with `vr_gun_beam_edit`); smooth turning per rendered frame (was per tick: judder); no crash on death (beam double free); New Game chapters.
- Verified with the null HMD / flat test only: nerve gas timer counts down (`cl_pdump` shows `m_iBonusProgress`); credits on the menu screen (`vrtest_credits.cfg`).
- Not exercised: the non-Frame controller bindings (Index, Touch, Cosmos, Vive, WMR, G2).
- Verified in the headset (later the same day): picking up cubes without the gun; title screen = black room with the flat flyby + menu on a screen.
- Testing: `vrtest_grab.cfg` (testchmb_a_00; waits out the frozen wake-up intro), `vrtest_title.cfg` (no map), `vr_debug_grab` presses the gun grip without controllers.

**Ideas and known gaps**
- The gun glow is a sprite plus material tint. The first-person viewmodel effects (beam, particle glow) aren't attached to the new model.
- A one-frame landing dip of about 6 units remains after jumps (client prediction).
- Bloom and colour correction are still off in VR. The effect needs eye-sized buffers.
- Comfort vignette.
- Left-handed mode exists (`vr_gun_hand 0`), but the user only uses the right hand.
