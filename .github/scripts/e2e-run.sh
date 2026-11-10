#!/usr/bin/env bash
#
# Runs the end-to-end checks (tests/e2e/obs_e2e.py) against one plugin package,
# inside a real OBS started headless under Xvfb.
#
# Usage: e2e-run.sh <plugin-tarball> <label> <results-dir>
#
# Expects OBS, xvfb and ffmpeg to be installed already (the workflow does that).
# Exit status is the checks' own: 0 only when every one passed.
set -euo pipefail

package="$1"
label="$2"
out="$3"
mkdir -p "$out"
out="$(cd "$out" && pwd)"
repo="$(cd "$(dirname "$0")/../.." && pwd)"

# The real HOME, not a temporary one: OBS takes its config folder from the
# account database, not from $HOME, so a throwaway HOME is simply not looked at.
# The CI runner is a fresh machine per job, so nothing leaks between runs. The
# plugin is installed exactly as the README says: extracted into $HOME.
: "${HOME:?}"
tar -xzf "$package" -C "$HOME"

cfg="$HOME/.config/obs-studio"
clips="$HOME/clips"
mkdir -p "$clips" "$cfg/basic/profiles/E2E" "$cfg/basic/scenes" \
  "$cfg/plugin_config/obs-websocket" "$cfg/plugin_config/obs-playlist-deck"

# Three distinct, bright clips of equal length with no audio. The test reads the
# program output after the last one, so it has to be something that is not black.
make_clip() {
  ffmpeg -loglevel error -y -f lavfi -i "color=c=$2:s=320x180:r=25:d=3" \
    -c:v libx264 -pix_fmt yuv420p -an "$clips/$1.mp4"
}
make_clip clip0 0xE02020
make_clip clip1 0x20E020
make_clip clip2 0x2060F0

# No first-run wizard, no password on the websocket.
cat > "$cfg/global.ini" <<EOF
[General]
FirstRun=true

[Basic]
Profile=E2E
ProfileDir=E2E
SceneCollection=E2E
SceneCollectionFile=E2E
EOF
# OBS 31+ keeps the chosen profile and scene collection in user.ini.
cp "$cfg/global.ini" "$cfg/user.ini"
cat > "$cfg/basic/profiles/E2E/basic.ini" <<EOF
[General]
Name=E2E

[Video]
BaseCX=640
BaseCY=360
OutputCX=640
OutputCY=360
FPSType=0
FPSCommon=30

[Output]
Mode=Simple
EOF
cat > "$cfg/plugin_config/obs-websocket/config.json" <<EOF
{"alerts_enabled": false, "auth_required": false, "first_load": false,
 "server_enabled": true, "server_password": "", "server_port": 4455}
EOF
# The deck binds to the media source by name; the mode is set by the test.
cat > "$cfg/plugin_config/obs-playlist-deck/settings.json" <<EOF
{"mode": 0, "source": "Deck Media", "enableProbe": false, "relativePaths": false}
EOF
cat > "$cfg/basic/scenes/E2E.json" <<EOF
{
  "name": "E2E",
  "current_scene": "Deck",
  "current_program_scene": "Deck",
  "scene_order": [{"name": "Deck"}, {"name": "Other"}],
  "sources": [
    {"id": "ffmpeg_source", "versioned_id": "ffmpeg_source", "name": "Deck Media",
     "settings": {"is_local_file": true, "local_file": "", "looping": false}},
    {"id": "scene", "versioned_id": "scene", "name": "Deck",
     "settings": {"id_counter": 1, "items": [
       {"name": "Deck Media", "id": 1, "visible": true,
        "pos": {"x": 0.0, "y": 0.0}, "scale": {"x": 2.0, "y": 2.0}}]}},
    {"id": "scene", "versioned_id": "scene", "name": "Other",
     "settings": {"id_counter": 0, "items": []}}
  ]
}
EOF

export LIBGL_ALWAYS_SOFTWARE=1
echo "::group::start OBS ($label)"
xvfb-run -a -s "-screen 0 1280x720x24" \
  obs --disable-shutdown-check --disable-missing-files-check --disable-updater \
  > "$out/obs-stdout.log" 2>&1 &
obs_pid=$!

# The websocket opens once the main window is up; the deck answers once its dock
# has been created, which is later still.
up=0
python3 - <<'PY' || up=$?
import json, sys, time
import websocket
deadline = time.time() + 120
while time.time() < deadline:
    try:
        ws = websocket.create_connection("ws://127.0.0.1:4455", timeout=5)
        ws.recv(); ws.send(json.dumps({"op": 1, "d": {"rpcVersion": 1}})); ws.recv()
        rid = "up"
        ws.send(json.dumps({"op": 6, "d": {"requestType": "CallVendorRequest", "requestId": rid,
            "requestData": {"vendorName": "obs-playlist-deck", "requestType": "GetStatus",
                            "requestData": {}}}}))
        reply = json.loads(ws.recv())["d"]
        if reply["requestStatus"]["result"] and reply["responseData"]["responseData"].get("ok"):
            print("OBS and the deck are up"); sys.exit(0)
    except Exception:
        pass
    time.sleep(2)
print("OBS or the deck did not come up"); sys.exit(1)
PY
echo "::endgroup::"

status=1
if [ "$up" -eq 0 ]; then
  set +e
  python3 "$repo/tests/e2e/obs_e2e.py" --label "$label" --json "$out/results.json" \
    --clips "$clips/clip0.mp4" "$clips/clip1.mp4" "$clips/clip2.mp4"
  status=$?
  set -e
fi

kill "$obs_pid" 2>/dev/null || true
sleep 2
# OBS's own log, with the plugin's trace lines in a test build.
cp "$cfg"/logs/*.txt "$out/" 2>/dev/null || true
exit "$status"
