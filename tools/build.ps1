# Builds the Portal VR client/server DLLs (Release, Win32) and prints errors.
#   .\build.ps1                 -> everything
#   .\build.ps1 -Targets client -> just one project (tier1, mathlib, raytrace, vgui_controls, client, server)
#   .\build.ps1 -Regenerate     -> rerun VPC first

param(
	[string[]]$Targets = @(),
	[switch]$Regenerate,
	[string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$repo = Resolve-Path (Join-Path $PSScriptRoot '..')
if ($Regenerate -or -not (Test-Path (Join-Path $repo 'sp\src\portalvr.sln'))) {
	& (Join-Path $PSScriptRoot 'genprojects.ps1')
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (-not $msbuild) { throw 'MSBuild not found (install Visual Studio with the C++ desktop workload)' }
$names = @{}
$targetArgs = @()
foreach ($t in $Targets) {
	$n = if ($names.ContainsKey($t)) { $names[$t] } else { $t }
	$targetArgs += "/t:$($n -replace '[ ()]', '_')"
}

$log = Join-Path $env:TEMP 'portalvr_build.log'
& $msbuild (Join-Path $repo 'sp\src\portalvr.sln') @targetArgs "/p:Configuration=$Configuration" /p:Platform=Win32 /m /v:m /nologo 2>&1 | Out-File $log -Encoding utf8
$code = $LASTEXITCODE

$errors = Select-String -Path $log -Pattern ': (fatal )?error ' | ForEach-Object { $_.Line.Trim() } | Select-Object -Unique
if ($errors) {
	Write-Host "$(@($errors).Count) error line(s):"
	$errors | Select-Object -First 60 | ForEach-Object { Write-Host $_ }
}
Write-Host "Build exit code $code (full log: $log)"
exit $code
