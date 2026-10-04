# Runs Portal VR against the SteamVR null headset for a while, then restores the user's
# SteamVR settings. Prints how long the game survived plus the crash log, if any.
param(
	[string]$Map = 'testchmb_a_02',
	[int]$Seconds = 45,
	[string]$Extra = ''
)
$r = Split-Path $PSScriptRoot -Parent
$g = Join-Path $r 'sp\game\portalvr'
& "$PSScriptRoot\nullhmd.ps1" on | Select-Object -Last 1
try {
	Remove-Item "$g\console.log", "$g\vr_log.txt", "$g\vr_crash.txt" -ErrorAction SilentlyContinue
	# The null headset has no play area, so on the first app SteamVR launches Room Setup as a
	# scene app and kills the game for it. Sacrifice one launch, close Room Setup, then test.
	& "$PSScriptRoot\launch.ps1" -Map $Map -Extra $Extra
	for ($i = 0; $i -lt 30 -and (Get-Process hl2 -ErrorAction SilentlyContinue); $i++) { Start-Sleep 1 }
	Stop-Process -Name hl2 -Force -ErrorAction SilentlyContinue
	Get-Process steamvr_room_setup -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
	Start-Sleep 3
	Remove-Item "$g\console.log", "$g\vr_log.txt", "$g\vr_crash.txt" -ErrorAction SilentlyContinue

	& "$PSScriptRoot\launch.ps1" -Map $Map -Extra $Extra
	$t0 = Get-Date
	while (((Get-Date) - $t0).TotalSeconds -lt $Seconds) {
		Start-Sleep 2
		if (-not (Get-Process hl2 -ErrorAction SilentlyContinue)) { "GAME EXITED after $([int]((Get-Date) - $t0).TotalSeconds)s"; break }
	}
	if (Get-Process hl2 -ErrorAction SilentlyContinue) { "Game alive after $Seconds s" }
}
finally {
	Stop-Process -Name hl2 -Force -ErrorAction SilentlyContinue
	Start-Sleep 2
	# SteamVR can respawn its processes for a moment; keep stopping them until they stay gone,
	# otherwise nullhmd off refuses and the null headset stays switched on.
	for ($i = 0; $i -lt 15; $i++) {
		$vr = Get-Process | Where-Object { $_.Name -match '^vr|steamvr_room' }
		if (-not $vr) { break }
		$vr | Stop-Process -Force -ErrorAction SilentlyContinue
		Start-Sleep 2
	}
	& "$PSScriptRoot\nullhmd.ps1" off | Select-Object -Last 1
}
if (Test-Path "$g\vr_crash.txt") { Get-Content "$g\vr_crash.txt" }
