# Security policy

## Supported versions

The latest release is the supported one. Fixes go into the next release; there
are no backports to older tags.

## Reporting a vulnerability

Please report privately through GitHub's
[**Report a vulnerability**](https://github.com/angeloruggieridj/obs-playlist-deck/security/advisories/new)
form rather than opening a public issue. A first reply should arrive within a
week; please allow reasonable time for a fix before disclosing.

Useful in a report: the OBS version and platform, the plugin version, what an
attacker would have to control to trigger it, and — where you have one — a
minimal reproduction.

## What the plugin actually exposes

Worth knowing before reporting, and worth reading if you are weighing the risk
of installing it:

- **No server, no browser source, no embedded web UI.** The plugin is a Qt dock
  inside OBS.
- **One outbound request:** an update check against `api.github.com` over HTTPS
  through libcurl, restricted to HTTPS on redirects, capped in redirects, time
  and response size. It sends nothing but the request itself. It is made once at
  startup and whenever you press *Check for updates*.
- **Remote control** is available only through **obs-websocket**, if you have
  installed and enabled it. Anyone who can authenticate to your obs-websocket
  can drive the deck: play items, add paths, and ask it to open a playlist file
  from a path they name (capped at 10 MB, and its titles then show in the dock).
  That is the trust model of obs-websocket itself — do not expose it to a
  network you do not control.
- **Files read and written:** playlist files you choose, plus `settings.json`
  and `session.json` in the plugin's own OBS config folder. Writes are atomic.
- **Media files** are opened read-only, by FFmpeg, to read their duration.

## Unsigned releases

Release packages are not code-signed — see
[docs/verification.md](docs/verification.md) for what ships instead (build
provenance attestation, per-asset digests, VirusTotal reports) and how to check
a download.
