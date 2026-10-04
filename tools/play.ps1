# Player entry point (launch.bat): finds Portal, links the mod into it, then starts Portal VR.
# Arguments are passed on to launch.ps1 (e.g. -Flat, -Map testchmb_a_08, -Console).
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'findportal.ps1')
$repo = Resolve-Path (Join-Path $PSScriptRoot '..')

$portal = Find-PortalDir
if (-not $portal) {
	throw 'Portal is not installed (or not where Steam keeps its games). Install Portal from Steam, then try again.'
}

# Prebuilt game DLLs ship in the repo; only build them if they are missing (needs Visual Studio).
if (-not (Test-Path (Join-Path $repo 'sp\game\portalvr\bin\client.dll'))) {
	Write-Host 'Game DLLs missing, building them (needs Visual Studio with C++)...'
	& (Join-Path $PSScriptRoot 'build.ps1')
}

& (Join-Path $PSScriptRoot 'deploy.ps1') -PortalDir $portal
& (Join-Path $PSScriptRoot 'launch.ps1') -PortalDir $portal @args
