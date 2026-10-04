# Makes the mod visible to the retail Portal install.
#
# Creates a directory junction  <Portal>\portalvr  ->  <repo>\sp\game\portalvr  so the build
# output (which VPC writes to sp\game\portalvr\bin) is used directly by the game. No retail
# file is modified; deleting the junction removes the mod.

param(
	[string]$PortalDir = 'C:\Program Files (x86)\Steam\steamapps\common\Portal'
)

$ErrorActionPreference = 'Stop'
$repo = Resolve-Path (Join-Path $PSScriptRoot '..')
$modSrc = Join-Path $repo 'sp\game\portalvr'
$modLink = Join-Path $PortalDir 'portalvr'

if (-not (Test-Path (Join-Path $PortalDir 'hl2.exe'))) { throw "Portal not found at $PortalDir" }

$bin = Join-Path $modSrc 'bin'
New-Item -ItemType Directory -Force $bin | Out-Null
Copy-Item (Join-Path $repo 'sp\src\thirdparty\openvr\bin\openvr_api.dll') $bin -Force

# The engine loads UI strings from resource\<mod folder>_<language>.txt; reuse Portal's.
foreach ($lang in Get-ChildItem (Join-Path $PortalDir 'portal\resource') -Filter 'portal_*.txt') {
	$dest = Join-Path $modSrc ('resource\portalvr_' + $lang.Name.Substring('portal_'.Length))
	# The New Game dialog looks up #<mod folder>_ChapterN_Title, so duplicate Portal's chapter titles.
	$text = [System.IO.File]::ReadAllText($lang.FullName)
	$text = [regex]::Replace($text, '(?m)^(\s*)"Portal_(Chapter\d+_Title)"(.*)$', { param($m)
		$m.Value.TrimEnd("`r") + "`r`n" + $m.Groups[1].Value + '"PortalVR_' + $m.Groups[2].Value + '"' + $m.Groups[3].Value.TrimEnd("`r") })
	[System.IO.File]::WriteAllText($dest, $text, [System.Text.Encoding]::Unicode)
}

$item = Get-Item $modLink -ErrorAction SilentlyContinue
if ($item) {
	if ($item.LinkType -ne 'Junction') { throw "$modLink exists and is not a junction; refusing to touch it." }
	if ($item.Target -ne $modSrc) {
		Remove-Item $modLink -Force
		$item = $null
	}
}
if (-not $item) {
	New-Item -ItemType Junction -Path $modLink -Target $modSrc | Out-Null
	Write-Host "Linked $modLink -> $modSrc"
}
else {
	Write-Host "Junction already in place: $modLink -> $modSrc"
}
