#!/usr/bin/env bash
# Registers (or with --remove, unregisters) the MotionVR Bridge SteamVR driver
# that sits next to this script, using SteamVR's own vrpathreg tool.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
driver_dir="$here/driver/motionvrbridge"
paths_file="${XDG_CONFIG_HOME:-$HOME/.config}/openvr/openvrpaths.vrpath"

if [[ ! -f "$paths_file" ]]; then
    echo "SteamVR was not found. Start SteamVR once, then run this script again." >&2
    exit 1
fi

runtime="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["runtime"][0])' "$paths_file")"
vrpathreg="$runtime/bin/vrpathreg.sh"
if [[ ! -x "$vrpathreg" ]]; then
    echo "vrpathreg.sh not found at $vrpathreg" >&2
    exit 1
fi

command=adddriver
[[ "${1:-}" == "--remove" ]] && command=removedriver
"$vrpathreg" "$command" "$driver_dir"
echo "SteamVR driver $command: $driver_dir"
