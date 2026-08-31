# Verifying a Playlist Deck download

> The releases are not code-signed. This page explains what that means on each
> platform, and the three independent ways to check a download for yourself.
> Back to the [README](../README.md).

Playlist Deck is a free, one-person project, and code-signing certificates are
neither free nor issued to projects: an Apple Developer membership is a yearly
fee, and a Windows OV/EV certificate costs more. So the released packages carry
**no publisher signature on any platform**. That is why your system treats them
as untrusted, and what you have to do about it:

| Platform | What you'll see | What to do |
|---|---|---|
| **macOS** | Gatekeeper quarantines anything downloaded from a browser. OBS then fails to load the plugin, usually silently — it simply never appears in the *Docks* menu. | Clear the quarantine attribute once, as shown in the [install step](../README.md#macos-universal): `xattr -dr com.apple.quarantine "$PLUGIN_DIR/obs-playlist-deck.plugin"`. The bundle is ad-hoc signed, so nothing else is needed. |
| **Windows** | The downloaded `.zip` is marked as coming from the internet; SmartScreen may warn, and some antivirus products flag unsigned DLLs on sight. | Right-click the zip → **Properties** → tick **Unblock**, then extract. If your antivirus quarantines the DLL, verify it first (below) and add an exclusion. |
| **Linux** | Nothing. There is no signature check to fail. | Just extract it. |

"Unsigned" means nobody paid to vouch for the file — it does not mean the file
is unverified. Every release is built in public by
[GitHub Actions](../.github/workflows/build_project.yml) from the tagged source,
and each one ships evidence you can check yourself:

**1. Integrity.** GitHub publishes a SHA-256 digest for every release asset, so
hash your download and compare:

```bash
# macOS / Linux
shasum -a 256 obs-playlist-deck-macos-universal.tar.gz
```
```powershell
# Windows
(Get-FileHash obs-playlist-deck-windows.zip -Algorithm SHA256).Hash
```
```bash
# what GitHub says it should be
gh release view v1.3.0 --repo angeloruggieridj/obs-playlist-deck --json assets \
  --jq '.assets[] | .name + "  " + .digest'
```

This catches a truncated or corrupted download. It is not evidence of who built
the file — the digest and the file come from the same place — which is what the
next step is for.

**2. Build provenance.** Each package is published with a signed
[GitHub attestation](https://docs.github.com/actions/security-guides/using-artifact-attestations)
binding it to the exact workflow run, commit and runner that produced it — proof
it came out of this repository's CI and not off someone's laptop. This is the
guarantee that actually replaces a publisher signature. Verify it with the
[GitHub CLI](https://cli.github.com/):

```bash
gh attestation verify obs-playlist-deck-macos-universal.tar.gz --repo angeloruggieridj/obs-playlist-deck
```

**3. Malware scan.** When a VirusTotal API key is configured for the repository,
each release is scanned across ~70 antivirus engines and the report links are
added to the release page. You can also upload any file to
[virustotal.com](https://www.virustotal.com/gui/home/upload) yourself — a handful
of engines flagging an unsigned DLL is a common false positive, which is exactly
why the provenance attestation matters more than a scan.

> The VirusTotal badge at the top of this page is static: it says the packages
> **are** scanned, not that any particular scan came back clean. VirusTotal has
> no live badge endpoint — the per-file reports linked from each release are the
> actual result, and they are what you should read.

> Provenance attestations are produced from **v1.2.6 onwards**. Earlier releases
> have GitHub's asset digests but no attestation.

Finally, nothing here is a black box: the plugin is MIT-licensed, the full source
is in this repository, and you can always
[build it yourself](../README.md#building-from-source).
