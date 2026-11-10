# Runs the end-to-end checks (obs_e2e.py) against one plugin build, in a
# throwaway *portable* copy of the OBS installed on this machine. Nothing of the
# normal OBS profile, plugins or settings is read or written.
#
#   run-local-windows.ps1 -Work <scratch dir> -Plugin <folder holding obs-playlist-deck\> -Label <name>
#
# <Plugin> is an unpacked obs-playlist-deck-windows.zip (the folder that contains
# the "obs-playlist-deck" directory). Needs ffmpeg on PATH and a Python with
# `websocket-client` and `pillow` (-Python).
param(
    [Parameter(Mandatory)] [string] $Work,
    [Parameter(Mandatory)] [string] $Plugin,
    [Parameter(Mandatory)] [string] $Label,
    [string] $Python = "python",
    [double] $ClipSeconds = 3,
    [string] $ObsDir = "C:\Program Files\obs-studio"
)
$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$obs = Join-Path $Work "obs"
if (-not (Test-Path $obs)) {
    robocopy $ObsDir $obs /E /NFL /NDL /NJH /NJS /NP | Out-Null
}
$cfg = Join-Path $obs "config\obs-studio"
if (Test-Path (Join-Path $obs "config")) { Remove-Item (Join-Path $obs "config") -Recurse -Force }
$clips = Join-Path $Work "clips"
New-Item -ItemType Directory -Force $clips, "$cfg\basic\profiles\E2E", "$cfg\basic\scenes",
    "$cfg\plugin_config\obs-websocket", "$cfg\plugin_config\obs-playlist-deck",
    "$cfg\plugins" | Out-Null

function New-Clip($name, $color) {
    $f = Join-Path $clips "$name-$ClipSeconds.mp4"
    if (-not (Test-Path $f)) {
        ffmpeg -loglevel error -y -f lavfi -i "color=c=${color}:s=320x180:r=25:d=${ClipSeconds}" `
            -c:v libx264 -pix_fmt yuv420p -an $f
    }
    $f
}
$c0 = New-Clip clip0 '0xE02020'
$c1 = New-Clip clip1 '0x20E020'
$c2 = New-Clip clip2 '0x2060F0'

# Installed into the copy's own plugin folders: that is the place a portable OBS
# is sure to look, and it keeps one build at a time from lingering in the next run.
$src = Join-Path $Plugin "obs-playlist-deck"
$dll = "$obs\obs-plugins\64bit\obs-playlist-deck.dll"
$dat = "$obs\data\obs-plugins\obs-playlist-deck"
Remove-Item $dll, $dat -Recurse -Force -ErrorAction SilentlyContinue
Copy-Item "$src\bin\64bit\obs-playlist-deck.dll" $dll
Copy-Item "$src\data" $dat -Recurse

@"
[General]
FirstRun=true

[Basic]
Profile=E2E
ProfileDir=E2E
SceneCollection=E2E
SceneCollectionFile=E2E
"@ | Set-Content "$cfg\global.ini" -Encoding ASCII
# OBS 31+ keeps the chosen profile and scene collection in user.ini.
Copy-Item "$cfg\global.ini" "$cfg\user.ini"
@"
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
"@ | Set-Content "$cfg\basic\profiles\E2E\basic.ini" -Encoding ASCII
'{"alerts_enabled": false, "auth_required": false, "first_load": false, "server_enabled": true, "server_password": "", "server_port": 4455}' |
    Set-Content "$cfg\plugin_config\obs-websocket\config.json" -Encoding ASCII
'{"mode": 0, "source": "Deck Media", "enableProbe": false, "relativePaths": false}' |
    Set-Content "$cfg\plugin_config\obs-playlist-deck\settings.json" -Encoding ASCII
@'
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
'@ | Set-Content "$cfg\basic\scenes\E2E.json" -Encoding ASCII

$results = Join-Path $Work "results-$Label.json"
if (Test-Path $results) { Remove-Item $results }
$proc = Start-Process "$obs\bin\64bit\obs64.exe" `
    -ArgumentList "--portable", "--disable-shutdown-check", "--disable-updater", "--multi" `
    -WorkingDirectory "$obs\bin\64bit" -PassThru
$status = 1
try {
    $ready = $false
    for ($i = 0; $i -lt 60 -and -not $ready; $i++) {
        Start-Sleep 2
        & $Python -c @"
import json, sys, websocket
try:
    ws = websocket.create_connection('ws://127.0.0.1:4455', timeout=5)
    ws.recv(); ws.send(json.dumps({'op': 1, 'd': {'rpcVersion': 1}})); ws.recv()
    ws.send(json.dumps({'op': 6, 'd': {'requestType': 'CallVendorRequest', 'requestId': 'u',
        'requestData': {'vendorName': 'obs-playlist-deck', 'requestType': 'GetStatus', 'requestData': {}}}}))
    r = json.loads(ws.recv())['d']
    sys.exit(0 if r['requestStatus']['result'] and r['responseData']['responseData'].get('ok') else 1)
except Exception:
    sys.exit(1)
"@
        $ready = ($LASTEXITCODE -eq 0)
    }
    if (-not $ready) { Write-Host "OBS or the deck did not come up"; }
    else {
        & $Python (Join-Path $here "obs_e2e.py") --label $Label --json $results --clip-seconds $ClipSeconds --clips $c0 $c1 $c2
        $status = $LASTEXITCODE
    }
} finally {
    Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    Start-Sleep 2
    Copy-Item (Get-ChildItem "$cfg\logs\*.txt" | Select-Object -Last 1) (Join-Path $Work "obs-$Label.log") -ErrorAction SilentlyContinue
}
exit $status
