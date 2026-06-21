# Playlist Deck — Stream Deck companion

An Elgato Stream Deck plugin that controls the OBS **Playlist Deck** plugin over
obs-websocket. Buildless (plain JS/HTML, no npm step).

## Actions

- **Next** / **Previous** — play the next / previous playlist item
- **Play / Pause** — toggle the bound media source
- **Stop** — stop playback
- **Play Item** — play a specific item by 0-based index (set in the action's
  Property Inspector)

## How it works

The plugin keeps one obs-websocket **v5** connection. On `Hello` it performs the
v5 authentication (`base64(sha256(base64(sha256(password+salt))+challenge))`)
via the Web Crypto API and sends `Identify`. Button presses send a `Request`
(op 6) with `requestType: "CallVendorRequest"` and
`requestData: { vendorName: "obs-playlist-deck", requestType, requestData }`,
hitting the vendor requests registered by the OBS plugin
(`Next`, `Previous`, `PlayPause`, `Stop`, `PlayIndex`, `Load`, `GetStatus`).

## Requirements

1. The **Playlist Deck** OBS plugin installed and OBS running.
2. obs-websocket enabled: OBS → **Tools → WebSocket Server Settings** → enable,
   note the port (default `4455`) and password.

## Install

Copy (or symlink) the `com.angeloruggieridj.playlist-deck.sdPlugin` folder into
the Stream Deck plugins directory, then restart the Stream Deck app:

- **macOS:** `~/Library/Application Support/com.elgato.StreamDeck/Plugins/`
- **Windows:** `%APPDATA%\Elgato\StreamDeck\Plugins\`

Drag the actions onto keys. Open any action's Property Inspector and enter the
OBS **host / port / password** once (stored globally and shared by all actions).

## Icons

`icons/*.svg` are the sources. The Stream Deck SDK expects PNGs
(`<name>.png` + `<name>@2x.png`, ~72×72 / 144×144). Generate them, e.g.:

```bash
for f in icons/*.svg; do
  n="${f%.svg}"
  rsvg-convert -w 72  -h 72  "$f" -o "$n.png"
  rsvg-convert -w 144 -h 144 "$f" -o "$n@2x.png"
done
```

(or use Inkscape / any SVG→PNG tool). Actions still work without the PNGs; they
just show a blank key image until the PNGs are generated.
