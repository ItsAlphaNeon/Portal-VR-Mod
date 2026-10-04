# Loads every Portal campaign map with the mod's DLLs (flat, no VR) and reports maps that
# fail to load, crash, or print errors. Each map runs in its own game process.
#   .\smoketest.ps1                       -> all campaign maps
#   .\smoketest.ps1 -Maps testchmb_a_00   -> just one
#   .\smoketest.ps1 -Extra '+vr_emulate 1'

param(
	[string[]]$Maps = @(
		'testchmb_a_00', 'testchmb_a_01', 'testchmb_a_02', 'testchmb_a_03', 'testchmb_a_04',
		'testchmb_a_05', 'testchmb_a_06', 'testchmb_a_07', 'testchmb_a_08', 'testchmb_a_09',
		'testchmb_a_10', 'testchmb_a_11', 'testchmb_a_13', 'testchmb_a_14', 'testchmb_a_15',
		'escape_00', 'escape_01', 'escape_02'),
	[int]$TimeoutSec = 90,
	[int]$WaitFrames = 600,
	[string]$Extra = '',
	[string]$PortalDir = 'C:\Program Files (x86)\Steam\steamapps\common\Portal'
)

$ErrorActionPreference = 'Stop'
$modDir = Join-Path $PortalDir 'portalvr'
$log = Join-Path $modDir 'console.log'
$outDir = Join-Path $PSScriptRoot '..\smoketest_logs'
New-Item -ItemType Directory -Force $outDir | Out-Null
$results = @()

foreach ($map in $Maps) {
	Remove-Item $log -ErrorAction SilentlyContinue
	$gameArgs = @('-game', 'portalvr', '-novid', '-window', '-w', '1024', '-h', '640', '-condebug', '-nosound',
		'+map', $map, '+wait', "$WaitFrames", '+echo', "SMOKETEST_OK_$map", '+quit')
	if ($Extra) { $gameArgs += $Extra.Split(' ', [System.StringSplitOptions]::RemoveEmptyEntries) }
	$proc = Start-Process -FilePath (Join-Path $PortalDir 'hl2.exe') -WorkingDirectory $PortalDir -ArgumentList $gameArgs -PassThru
	$exited = $proc.WaitForExit($TimeoutSec * 1000)
	if (-not $exited) { $proc.Kill(); $proc.WaitForExit() }

	$text = if (Test-Path $log) { Get-Content $log -Raw } else { '' }
	Copy-Item $log (Join-Path $outDir "$map.log") -ErrorAction SilentlyContinue
	$errors = ($text -split "`n") | Where-Object { $_ -match '(?i)error|assert|failed|crash|missing|not found|bad ' } | Select-Object -Unique
	$status = if (-not $exited) { 'TIMEOUT' }
		elseif ($text -notmatch "SMOKETEST_OK_$map") { 'FAILED' }
		elseif ($errors) { 'OK (warnings)' }
		else { 'OK' }
	$results += [pscustomobject]@{ Map = $map; Status = $status; ExitCode = $proc.ExitCode; Issues = ($errors | Select-Object -First 3) -join ' | ' }
	Write-Host ("{0,-24} {1}" -f $map, $status)
}

$results | Format-Table -AutoSize -Wrap
