#!/usr/bin/env bash
# Linux/Proton counterpart of deploy.ps1: makes the mod visible to the Portal install
# that Steam runs through Proton.
#
#   tools/deploy.sh [PortalDir]
#
# Creates the symlink <Portal>/portalvr -> <repo>/sp/game/portalvr, copies openvr_api.dll
# and generates the localization files. No retail file is modified; deleting the symlink
# removes the mod. Portal must be forced to use Proton (Properties -> Compatibility), as
# the native Linux build cannot load the mod's DLLs. Needs python3.

set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
modSrc="$repo/sp/game/portalvr"

find_portal() {
	local root vdf lib
	for root in "$HOME/.steam/steam" "$HOME/.local/share/Steam" \
		"$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam"; do
		vdf="$root/steamapps/libraryfolders.vdf"
		[ -f "$vdf" ] || continue
		while IFS= read -r lib; do
			if [ -f "$lib/steamapps/common/Portal/hl2.exe" ]; then
				echo "$lib/steamapps/common/Portal"
				return 0
			fi
		done < <(sed -n 's/^[[:space:]]*"path"[[:space:]]*"\(.*\)".*/\1/p' "$vdf")
	done
	return 1
}

portalDir="${1:-}"
if [ -z "$portalDir" ]; then
	portalDir="$(find_portal)" || { echo "Portal not found; pass the Portal folder as an argument" >&2; exit 1; }
fi
if [ ! -f "$portalDir/hl2.exe" ]; then
	echo "No hl2.exe in $portalDir. Force Proton in Portal's Compatibility settings and let Steam" >&2
	echo "download the Windows build, then run this again." >&2
	exit 1
fi

mkdir -p "$modSrc/bin"
cp -f "$repo/sp/src/thirdparty/openvr/bin/openvr_api.dll" "$modSrc/bin/"

# The engine loads UI strings from resource/<mod folder>_<language>.txt; reuse Portal's, and
# duplicate the chapter titles as #PortalVR_ChapterN_Title for the New Game dialog.
python3 - "$portalDir/portal/resource" "$modSrc/resource" <<'PYEOF'
import glob, os, re, sys
src, dst = sys.argv[1], sys.argv[2]
pat = re.compile(r'^(\s*)"Portal_(Chapter\d+_Title)"(.*)$', re.M)
def dup(m):
    return m.group(0).rstrip('\r') + '\r\n' + m.group(1) + '"PortalVR_' + m.group(2) + '"' + m.group(3).rstrip('\r')
for path in glob.glob(os.path.join(src, 'portal_*.txt')):
    with open(path, 'rb') as f:
        text = f.read().decode('utf-16')
    out = os.path.join(dst, 'portalvr_' + os.path.basename(path)[len('portal_'):])
    with open(out, 'wb') as f:
        f.write(pat.sub(dup, text).encode('utf-16'))
PYEOF

modLink="$portalDir/portalvr"
if [ -L "$modLink" ]; then
	ln -sfn "$modSrc" "$modLink"
	echo "Symlink updated: $modLink -> $modSrc"
elif [ -e "$modLink" ]; then
	echo "$modLink exists and is not a symlink; refusing to touch it." >&2
	exit 1
else
	ln -s "$modSrc" "$modLink"
	echo "Linked $modLink -> $modSrc"
fi

echo
echo "Set Portal's Steam launch options to:"
echo "  %command% -game portalvr -vr -novid +mat_queue_mode 0 +fps_max 0 +mat_vsync 0"
echo "Start SteamVR first, then launch Portal from Steam."
