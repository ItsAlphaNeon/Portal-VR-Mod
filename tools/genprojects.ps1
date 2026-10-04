# Generates the Visual Studio projects for the Portal VR mod.
#
# VPC (Source SDK 2013) only knows how to emit VS2013 projects and fails to write the
# solution when VS2013 is not installed, so this script runs VPC, retargets the
# generated .vcxproj files to the VS2022 toolset (v143) and writes portalvr.sln itself.

$ErrorActionPreference = 'Stop'
$src = Join-Path $PSScriptRoot '..\sp\src' | Resolve-Path
Push-Location $src
try {
	# VPC exits non-zero because it can't find the VS2013 registry key for the .sln; the
	# projects themselves are written fine, so only check that they exist.
	& .\devtools\bin\vpc.exe /hl2 +game 2>&1 | Where-Object { $_ -match 'ERROR|Saving' } | ForEach-Object { Write-Host $_ }

	$projects = @(
		@{ Name = 'tier1';               Path = 'tier1\tier1.vcxproj';                       Deps = @() },
		@{ Name = 'mathlib';             Path = 'mathlib\mathlib.vcxproj';                   Deps = @() },
		@{ Name = 'raytrace';            Path = 'raytrace\raytrace.vcxproj';                 Deps = @() },
		@{ Name = 'vgui_controls';       Path = 'vgui2\vgui_controls\vgui_controls.vcxproj'; Deps = @() },
		@{ Name = 'client';              Path = 'game\client\client_hl2.vcxproj';            Deps = @('tier1', 'mathlib', 'vgui_controls') },
		@{ Name = 'server';              Path = 'game\server\server_hl2.vcxproj';            Deps = @('tier1', 'mathlib') }
	)

	foreach ($p in $projects) {
		if (-not (Test-Path $p.Path)) { throw "VPC did not generate $($p.Path)" }
		$xml = Get-Content $p.Path -Raw
		$xml = $xml -replace '<PlatformToolset>v120(_xp)?</PlatformToolset>', '<PlatformToolset>v143</PlatformToolset>'
		# The 2013 code predates modern MSVC conformance rules and warnings.
		$xml = $xml -replace '<TreatWarningAsError>true</TreatWarningAsError>', '<TreatWarningAsError>false</TreatWarningAsError>'
		$xml = $xml -replace '(<AdditionalOptions>[^<]*/Gw)</AdditionalOptions>', '$1 /Zc:threadSafeInit- /permissive /Zc:strictStrings- /Zc:__cplusplus- /wd4005 /wd4838 /wd4091 /wd5208 /wd4996</AdditionalOptions>'
		# VS2013 kept ml.exe in $(VCInstallDir)bin; newer toolsets put it on the build PATH.
		$xml = $xml.Replace('&quot;$(VCInstallDir)bin\ml.exe&quot;', 'ml.exe')
		# Prebuilt VS2013 static libs (tier2, dmxloader, ...) need the pre-UCRT stdio symbols.
		if ($p.Path -like 'game\*') {
			$xml = $xml -replace '<AdditionalDependencies>;', '<AdditionalDependencies>legacy_stdio_definitions.lib;'
		}
		# particles.lib (VS2013) carries its own hypot(), which the UCRT also defines.
		if ($p.Name -eq 'client') {
			$xml = $xml -replace '<AdditionalOptions> /ignore:4221</AdditionalOptions>', '<AdditionalOptions> /ignore:4221 /FORCE:MULTIPLE /ignore:4006 /ignore:4088</AdditionalOptions>'
		}
		if ($xml -notmatch 'WindowsTargetPlatformVersion') {
			$xml = $xml -replace '(<ProjectGuid>[^<]+</ProjectGuid>)', "`$1`r`n    <WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>"
		}
		Set-Content -Path $p.Path -Value $xml -NoNewline -Encoding UTF8
		$p.Guid = ([regex]::Match($xml, '<ProjectGuid>(\{[^}]+\})</ProjectGuid>')).Groups[1].Value
	}

	# Write the solution.
	$cppType = '{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}'
	$sb = [System.Text.StringBuilder]::new()
	[void]$sb.AppendLine('Microsoft Visual Studio Solution File, Format Version 12.00')
	[void]$sb.AppendLine('# Visual Studio Version 17')
	foreach ($p in $projects) {
		[void]$sb.AppendLine("Project(`"$cppType`") = `"$($p.Name)`", `"$($p.Path)`", `"$($p.Guid)`"")
		if ($p.Deps.Count -gt 0) {
			[void]$sb.AppendLine("`tProjectSection(ProjectDependencies) = postProject")
			foreach ($d in $p.Deps) {
				$g = ($projects | Where-Object { $_.Name -eq $d }).Guid
				[void]$sb.AppendLine("`t`t$g = $g")
			}
			[void]$sb.AppendLine("`tEndProjectSection")
		}
		[void]$sb.AppendLine('EndProject')
	}
	[void]$sb.AppendLine('Global')
	[void]$sb.AppendLine("`tGlobalSection(SolutionConfigurationPlatforms) = preSolution")
	[void]$sb.AppendLine("`t`tDebug|Win32 = Debug|Win32")
	[void]$sb.AppendLine("`t`tRelease|Win32 = Release|Win32")
	[void]$sb.AppendLine("`tEndGlobalSection")
	[void]$sb.AppendLine("`tGlobalSection(ProjectConfigurationPlatforms) = postSolution")
	foreach ($p in $projects) {
		foreach ($c in 'Debug', 'Release') {
			[void]$sb.AppendLine("`t`t$($p.Guid).$c|Win32.ActiveCfg = $c|Win32")
			[void]$sb.AppendLine("`t`t$($p.Guid).$c|Win32.Build.0 = $c|Win32")
		}
	}
	[void]$sb.AppendLine("`tEndGlobalSection")
	[void]$sb.AppendLine('EndGlobal')
	Set-Content -Path 'portalvr.sln' -Value $sb.ToString() -Encoding UTF8
	Write-Host "Wrote $(Join-Path $src 'portalvr.sln')"
}
finally {
	Pop-Location
}
