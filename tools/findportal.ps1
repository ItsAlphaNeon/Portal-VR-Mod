# Finds the retail Portal install: the default Steam folder, else every Steam library listed
# in Steam's libraryfolders.vdf. Dot-source it and call Find-PortalDir.
function Find-PortalDir {
	$candidates = @('C:\Program Files (x86)\Steam\steamapps\common\Portal')
	$steam = $null
	foreach ($key in 'HKCU:\Software\Valve\Steam', 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam') {
		$item = Get-ItemProperty $key -ErrorAction SilentlyContinue
		if ($item.SteamPath) { $steam = $item.SteamPath; break }
		if ($item.InstallPath) { $steam = $item.InstallPath; break }
	}
	if ($steam) {
		$candidates += Join-Path $steam 'steamapps\common\Portal'
		$vdf = Join-Path $steam 'steamapps\libraryfolders.vdf'
		if (Test-Path $vdf) {
			foreach ($m in [regex]::Matches((Get-Content $vdf -Raw), '"path"\s+"([^"]+)"')) {
				$candidates += Join-Path ($m.Groups[1].Value -replace '\\\\', '\') 'steamapps\common\Portal'
			}
		}
	}
	foreach ($dir in $candidates) {
		if (Test-Path (Join-Path $dir 'hl2.exe')) { return (Resolve-Path $dir).Path }
	}
	return $null
}
