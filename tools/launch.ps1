# Launches Portal VR on the retail Portal engine.
#   .\launch.ps1                    -> VR mode, main menu
#   .\launch.ps1 -Flat              -> no VR (desktop testing)
#   .\launch.ps1 -Map testchmb_a_00 -> load straight into a map
#   .\launch.ps1 -Extra '+vr_emulate 1'
#   .\launch.ps1 -Console          -> open the developer console at startup

param(
	[switch]$Flat,
	[string]$Map,
	[string]$Extra = '',
	[switch]$Console,
	[string]$PortalDir = ''	# default: found through Steam (findportal.ps1)
)

. (Join-Path $PSScriptRoot 'findportal.ps1')
if (-not $PortalDir) { $PortalDir = Find-PortalDir }
if (-not $PortalDir) { throw 'Portal not found; pass -PortalDir' }

$gameArgs = @('-game', 'portalvr', '-novid', '-window', '-w', '1280', '-h', '720', '-condebug',
	'+mat_queue_mode', '0', '+fps_max', '0', '+mat_vsync', '0')
if (-not $Flat) { $gameArgs += '-vr' }
if ($Console) { $gameArgs += '-console' }
if ($Map) { $gameArgs += @('+map', $Map) }
if ($Extra) { $gameArgs += $Extra.Split(' ', [System.StringSplitOptions]::RemoveEmptyEntries) }

Start-Process -FilePath (Join-Path $PortalDir 'hl2.exe') -WorkingDirectory $PortalDir -ArgumentList $gameArgs
