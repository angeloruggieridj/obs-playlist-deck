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

### Connection handling

- **Reconnects on its own**, with exponential backoff from 1 s to 30 s, so
  starting OBS after the Stream Deck app just works.
- **A press made while OBS is away is dropped**, not stored. It used to be
  queued: every press during an outage then fired at once on the next successful
  connection — seven `Next` actions in a row, live, minutes later.
- **A heartbeat** (`GetStatus` every 3 s) both proves the link is alive and
  keeps the key titles current.
- **The keys say what is going on**: the Play/Pause key shows the clip that is
  playing, Next shows what comes after it, Play Item marks the item that is
  current, and all of them read `offline` when OBS cannot be reached. Pressing a
  key while offline shows the Stream Deck alert instead of pretending it worked.

To see it for yourself: close OBS, press **Next** three times, then start OBS.
Nothing should fire on its own; the keys go from `offline` to showing titles
within a few seconds.

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
OBS **host / port / password** once (stored globally and shared by all actions);
fields save as you type. Press **Test connection** to check them — it tells apart
"cannot reach OBS", "rejected, check the password" and "OBS answered, Playlist
Deck plugin not found", which is otherwise a guessing game.

> The password is kept in the Stream Deck app's global settings, in the clear,
> like every other Stream Deck plugin's credentials. Treat it as local
> configuration, and do not expose obs-websocket outside a network you control.

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
