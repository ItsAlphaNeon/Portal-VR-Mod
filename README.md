# Portal VR

A full 6DOF, standing-roomscale VR mod for the original **Portal (2007)**, built for SteamVR and tuned on the **Steam Frame**. You hold the portal gun in your hand, walk around your room, and play the whole original campaign.

It runs on your own Steam copy of Portal **without changing any of its files**. It's a separate mod folder (`Portal\portalvr`) with its own `client.dll` and `server.dll`, rebuilt from [SonicEraZoR/Portal-Base](https://github.com/SonicEraZoR/Portal-Base) (the Portal 1 code ported to Source SDK 2013) with OpenVR built in.

> Personal project, not for distribution. It builds on leaked Portal source code via Portal-Base. You need to own Portal on Steam.

---

## Quick start (just play)

1. Install **Portal** and **SteamVR** from Steam, and run Portal once.
2. Download this repository (green *Code* button → *Download ZIP*, then unzip it somewhere with a short path such as `C:\Games\PortalVR` or your Downloads folder, or `git clone`; very deep folders hit Windows' path length limit).
3. Turn on your headset and controllers, start SteamVR, then double-click **`launch.bat`**.

`launch.bat` finds Portal through Steam, links the mod into it (it never changes Portal's own files), and starts it in VR. The game DLLs are prebuilt, so you don't need Visual Studio. To remove the mod, delete the `portalvr` folder link inside your Portal install folder.

If you move or re-download the repository, just run `launch.bat` again; it re-links the mod.

---

## Features

- **Stereo 6DOF rendering through SteamVR**
  - Uses the per-eye resolution SteamVR recommends.
  - Each eye renders into its own target, so glass, water and other screen-space refraction look right in VR.
- **Roomscale**
  - Walk around physically and the player's body follows your head; walls push you back.
  - Lean over ledges, crouch for real (ducking the hull for vents).
- **Smooth locomotion** with the stick, toward where you look or where your left hand points. **Snap turn or smooth turn.** No teleport.
- **Seated mode**: your current head height becomes Chell's eye height. Recentering measures it again.
- **Hand-held portal gun**
  - Uses the RTX Portal gun model with portal-coloured glowing parts.
  - Kicks back and flicks its claws on every shot, and its claws open and jitter while holding an object.
  - Portals are fired from the muzzle along the barrel.
  - The gun shows up through portals the same way props do: reach through a portal and the gun comes out of the other one.
- **Gun calibration in the headset**
  - Grab the gun model with your other hand, put it where it feels right, and turn the aim laser with the stick.
  - Without a saved calibration, the gun is placed from SteamVR's hand skeleton.
- **Picking things up with the gun**: press the grip to pick up what the gun points at (or press a button), and press again to drop. Held objects float in front of the barrel like in the original game.
- **Fizzlers can't be cheated.** Reaching through an emancipation grid and firing still fizzles the shot.
- **Smooth jumps.** Source's automatic mid-air "duck-jump" no longer jolts the camera.
- **Going through floor/ceiling portals**, two options:
  - **Instant** (default): you come out facing the right way and the horizon stays level.
  - **Original**: the view turns with the portal and then rolls back level, like the flat game.
- **End credits** roll on the menu screen, in a black void.
- **Menus in VR**
  - The pause and main menus appear on a panel in the world; you point the gun at them and the trigger clicks.
  - The HUD, subtitles and hints are on a panel that follows your gaze.
- **VR Settings window**, reachable from the main and pause menus (*VR SETTINGS*).
- **No body**: you see your controllers and the floating gun, also through portals.
- **Original gameplay** apart from the VR changes: no reload-to-clear, no rapid fire, no HL2 crosshair, Portal gamerules.

---

## Controls

Everything that affects the game is on the **right** controller. The left hand only walks. Default bindings ship for the controllers below, and you can rebind everything in the SteamVR binding UI.

| | Steam Frame | Valve Index | Quest / Rift, HP Reverb G2 | Vive Cosmos | Vive wands | Windows MR |
|---|---|---|---|---|---|---|
| Blue portal (and menu click) | right trigger | right trigger | right trigger | right trigger | right trigger | right trigger |
| Orange portal | right bumper | right trackpad click | B | right bumper | right menu button | right menu button |
| Pick up / drop | right grip | right grip (squeeze) | right grip | right grip | right grip | right grip |
| Jump | A | A | A | A | right trackpad up | right trackpad up |
| Crouch (toggle) | B | B | right stick click | B | right trackpad down | right trackpad down |
| Walk | left stick | left stick | left stick | left stick | left trackpad | left stick |
| Turn | right stick | right stick | right stick | right stick | right trackpad | right stick |
| Recenter | right stick click | right stick click | left stick click | right stick click | left trackpad click | right stick click |
| Pause menu | menu | left B | left menu | left menu | left menu | left menu |

The gun placement defaults were calibrated on the Steam Frame. If it sits oddly in your hand on another controller, use *VR SETTINGS → Calibrate gun position*.

### Steam Frame (full list)

| Input | Action |
|---|---|
| **Right trigger** | Fire blue portal · click in menus |
| **Right bumper** | Fire orange portal |
| **Right grip** (press) | Pick up / drop object · press buttons |
| **A** | Jump |
| **B** | Crouch (toggle) |
| **Right stick** left/right | Snap or smooth turn |
| **Right stick click** | Recenter (seated mode: also measure your height) |
| **Menu** | Pause menu (point the gun at it; trigger clicks) |
| **Left stick** | Walk |
| **Left View button** | Show/hide HUD panel |
| **Left D-pad up / down** | Quicksave / quickload |
| **Hold both grips + click right stick** | Start/stop gun calibration |
| Physically crouch | Crouch |

The binding files are generated by `tools/gen_bindings.py` (`sp/game/portalvr/actions/`).

### Gun calibration

Start it with *VR SETTINGS → Calibrate gun position*, with both grips plus a right-stick click, or by typing `vr_gun_calibrate` in the console.

| Input | Action |
|---|---|
| **Left grip** (hold) | Grab the gun model with your left hand; let go to drop it onto your right hand where it is |
| **Right stick** | Turn the yellow aim laser (where portals will go) |
| **A** (jump button) | Save and exit (writes `portalvr/cfg/vr_gun_calibration.cfg`) |
| **B** (crouch button) | Reset to the hand-skeleton placement |

---

## VR Settings window

Main menu or pause menu → **VR SETTINGS**. Changes apply immediately.

- Turning: snap or smooth
- Snap turn angle (15–90°)
- Smooth turn speed
- Walk direction: where you look, or where your left hand points
- Floor/ceiling portals: instant and level, or the original rolling view
- Seated mode
- Show the HUD panel
- Show the controller models
- Animated, glowing portal gun
- Recenter / measure height
- Calibrate gun position

---

## Building from source

Only needed to change the code; the prebuilt `client.dll` / `server.dll` are committed in `sp/game/portalvr/bin`.

Requirements:
- Portal on Steam (Windows)
- SteamVR
- Visual Studio 2022 or 2026 with the C++ desktop workload (the newest installed toolset is used), to build
- Python 3, to build
- Blender 5.x, only to rebuild the gun model

```powershell
# 1. Build client.dll / server.dll (Release, Win32)
.\tools\build.ps1                      # -Regenerate re-runs VPC after editing .vpc files

# 2. Link the mod folder into Portal, copy openvr_api.dll and the localization files
.\tools\deploy.ps1

# 3. (optional) rebuild the hand-held gun model; the converted model is already in the repo
.\tools\gunmodel\build.ps1             # needs Blender; paths are parameters

# 4. Play (launch.bat runs deploy + launch)
.\tools\launch.ps1                     # VR, main menu
.\tools\launch.ps1 -Console            # with the developer console
.\tools\launch.ps1 -Map testchmb_a_00  # straight into a map
.\tools\launch.ps1 -Flat               # no VR (desktop testing)
```

`launch.ps1` runs `hl2.exe -game portalvr -vr -window -novid +mat_queue_mode 0 +fps_max 0 +mat_vsync 0`. You can also make a Steam shortcut to `hl2.exe` with those arguments.

---

## Console reference

**Comfort and movement**

| Variable | Default | Meaning |
|---|---|---|
| `vr_turn_mode` | 0 | 0 = snap turn, 1 = smooth turn |
| `vr_snap_turn_angle` | 45 | Degrees per snap turn |
| `vr_smooth_turn_speed` | 180 | Smooth turn speed (degrees per second) |
| `vr_move_hand_relative` | 0 | 0 = walk where you look, 1 = walk where your left hand points |
| `vr_portal_view_mode` | 0 | 0 = instant level exit, 1 = original rotating view |
| `vr_portal_lerp_time` | 0.8 | Seconds the view takes to roll back level in mode 1 |
| `vr_seated` | 0 | Seated mode |
| `vr_eye_height` | 64 | Eye height that seated mode lifts you to |
| `vr_crouch_height` | 44 | Head height below which you physically crouch |
| `vr_roomscale_deadzone` | 4 | How far your head can lean before your body follows |
| `vr_world_scale` | 1.0 | World size relative to you |

**Gun**

| Variable | Default | Meaning |
|---|---|---|
| `vr_gun_x/y/z`, `vr_gun_pitch/yaw/roll` | calibrated | Gun model pose in the controller's grip space |
| `vr_gun_aim_pitch/yaw` | calibrated | Aim direction relative to the gun model |
| `vr_gun_calibrated` | 1 | 0 = place the gun from the SteamVR hand skeleton |
| `vr_gun_scale` | 1 | Gun model size |
| `vr_gun_anim` | 1 | Claw/recoil animation and portal-coloured glow |
| `vr_gun_glow_x/y/z`, `vr_gun_glow_size` | tuned | Core glow sprite (gun model space) |
| `vr_gun_beam_claw1/2/3`, `vr_gun_beam_end_x/y/z` | tuned | Grab electricity points (`vr_gun_beam_edit` to adjust) |
| `vr_gun_grab_distance` | 128 | How far the gun reaches to pick things up |

**HUD and display**

| Variable | Default | Meaning |
|---|---|---|
| `vr_hud_visible` | 1 | Show the HUD panel |
| `vr_hud_distance`, `vr_hud_width`, `vr_hud_pitch`, `vr_hud_follow_angle` | | HUD panel placement |
| `vr_menu_distance`, `vr_menu_width` | | Menu panel placement |
| `vr_show_controllers` | 1 | Draw the controller models: 0 = never, 1 = always, 2 = only while calibrating |
| `vr_mirror` | 1 | Show the left eye in the desktop window |
| `vr_znear` | 2 | Near clip plane |

**Commands**

| Command | Meaning |
|---|---|
| `vr_settings` | Open the VR Settings window |
| `vr_recenter` | Put your body back under your head (and measure height when seated) |
| `vr_gun_calibrate` | Start or stop gun calibration |
| `vr_gun_autoplace` | Place the gun from the hand skeleton (forgets the calibration) |
| `vr_gun_beam_edit` | Adjust the grab electricity (right grip = next point, sticks move it, A saves, B resets) |
| `vr_status` | Print runtime status: resolution, D3D, poses, controllers, skeleton |
| `vr_dump_eyes [name]` | Save the frame sent to the headset as `portalvr/<name>.bmp` |

The debug and testing tools are described in [CLAUDE.md](CLAUDE.md).

---

## Repository layout

| Path | Contents |
|---|---|
| `sp/src/game/client/vr/` | OpenVR runtime (`vr_openvr.*`), D3D9Ex→D3D11 frame sharing (`vr_d3d.*`), VR Settings window |
| `sp/src/game/client/client_virtualreality.*` | The VR client: tracking→world mapping, stereo views, input, gun model and animation, calibration, menus and HUD, overlays |
| `sp/src/game/shared/vr/vr_usercmd.*` | VR data carried in each user command (head/hand poses, aim, buttons, roomscale movement) |
| `sp/src/game/server/portal/portal_player.cpp` | Server side: VR eye and aim, gun pickup, fizzler check |
| `sp/game/portalvr/` | The mod folder: `gameinfo.txt`, cfg, SteamVR action manifest and bindings, menu, materials and models |
| `tools/` | Build, deploy, launch and test scripts, the gun model pipeline, VPK extractor |

## Credits

- Valve, for Portal and the Source SDK
- SonicEraZoR, for Portal-Base
- The Portal RTX gun asset (ripped from Portal with RTX)
- OpenVR (BSD-3-Clause)
