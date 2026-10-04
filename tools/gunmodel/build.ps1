# Portal VR: rebuilds models/vr/portalgun_rtx.mdl and its materials from the RTX portal gun asset.
param(
	[string]$Blend = "C:\Users\Neon\Downloads\portal-rtx-assets-portal-gun\source\Sketchfab_2023_05_08_17_26_35.blend",
	[string]$Textures = "C:\Users\Neon\Downloads\portal-rtx-assets-portal-gun\textures",
	[string]$Blender = "C:\Program Files\Blender Foundation\Blender 5.1\blender.exe",
	[string]$Portal = "C:\Program Files (x86)\Steam\steamapps\common\Portal"
)
$ErrorActionPreference = "Stop"
$here = $PSScriptRoot
$out = Join-Path $here "build"
$game = Join-Path $here "..\..\sp\game\portalvr" | Resolve-Path

& $Blender -b --factory-startup $Blend --python (Join-Path $here "build_gun_model.py") -- $Textures $out | Select-String "BBOX|MUZZLE|TRIS|PRONG|CORE|DONE|Error"
Copy-Item -Recurse -Force (Join-Path $out "materials\*") (Join-Path $game "materials")
Push-Location $out
& "$Portal\bin\studiomdl.exe" -nop4 -game "$game" portalgun_rtx.qc | Select-Object -Last 2
Pop-Location
