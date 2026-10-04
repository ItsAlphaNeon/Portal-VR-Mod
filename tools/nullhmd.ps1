# Switches SteamVR to its built-in virtual ("null") headset for desktop testing, and back.
#   .\nullhmd.ps1 on   -> backs up steamvr.vrsettings, enables the null HMD
#   .\nullhmd.ps1 off  -> restores the exact backup (your real headset setup)
# SteamVR must not be running while switching.

param([Parameter(Mandatory)][ValidateSet('on', 'off')][string]$Mode)

$ErrorActionPreference = 'Stop'
$settings = 'C:\Program Files (x86)\Steam\config\steamvr.vrsettings'
$backup = Join-Path $PSScriptRoot '..\steamvr.vrsettings.backup'

if (Get-Process vrserver, vrcompositor, vrmonitor -ErrorAction SilentlyContinue) {
	throw 'SteamVR is running; close it first.'
}

if ($Mode -eq 'on') {
	if (Test-Path $backup) { throw "A backup already exists at $backup - run 'off' first." }
	Copy-Item $settings $backup
	$json = Get-Content $settings -Raw | ConvertFrom-Json
	function Set-Prop($obj, $name, $value) {
		if ($obj.PSObject.Properties[$name]) { $obj.$name = $value } else { $obj | Add-Member -NotePropertyName $name -NotePropertyValue $value }
	}
	if (-not $json.PSObject.Properties['steamvr']) { $json | Add-Member -NotePropertyName steamvr -NotePropertyValue ([pscustomobject]@{}) }
	Set-Prop $json.steamvr 'forcedDriver' 'null'
	Set-Prop $json.steamvr 'activateMultipleDrivers' $true
	Set-Prop $json.steamvr 'requireHmd' $true
	Set-Prop $json 'driver_null' ([pscustomobject]@{
		enable = $true; serialNumber = 'Null Serial Number'; modelNumber = 'Null Model Number'
		windowX = 100; windowY = 100; windowWidth = 1920; windowHeight = 1080
		renderWidth = 1440; renderHeight = 1600; secondsFromVsyncToPhotons = 0.1; displayFrequency = 90
	})
	$json | ConvertTo-Json -Depth 20 | Set-Content $settings -Encoding UTF8
	Write-Host "Null HMD enabled (backup: $backup)"
}
else {
	if (-not (Test-Path $backup)) { throw "No backup at $backup" }
	Copy-Item $backup $settings -Force
	Remove-Item $backup
	Write-Host 'Original SteamVR settings restored.'
}
