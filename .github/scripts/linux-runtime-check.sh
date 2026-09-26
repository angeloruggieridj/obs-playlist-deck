#!/usr/bin/env bash
#
# Installs each Ubuntu .deb that OBS publishes for a release into a container
# of that Ubuntu, unpacks our package for the same Ubuntu into $HOME as the
# README says, and asks the dynamic linker to resolve every library and symbol
# the plugin imports. Both copies are checked: the OBS 33 layout and the legacy
# one.
#
# Usage: linux-runtime-check.sh <packages-dir> <required-obs-version> [optional-obs-version]
#
# A failure against the required version fails the script. The optional one is
# a prerelease: its result is reported as a warning, never as a failure.
set -euo pipefail

packages="$1"
required="$2"
optional="${3:-}"

failed_required=0

check_version() {
  local version="$1" kind="$2" debs deb ubuntu package
  debs="$(gh release view "$version" --repo obsproject/obs-studio --json assets \
    --jq '.assets[].name | select(test("^OBS-Studio-.*-Ubuntu-[0-9.]+-x86_64\\.deb$"))')"
  if [ -z "$debs" ]; then
    report "$kind" "OBS $version publishes no Ubuntu .deb to check against"
    return
  fi

  for deb in $debs; do
    ubuntu="$(printf '%s' "$deb" | sed -E 's/.*-Ubuntu-([0-9.]+)-x86_64\.deb$/\1/')"
    package="$packages/obs-playlist-deck-linux-ubuntu-$ubuntu-x86_64.tar.gz"
    if [ ! -f "$package" ]; then
      report "$kind" "OBS $version ships for Ubuntu $ubuntu, and there is no package for it"
      continue
    fi

    echo "::group::OBS $version on Ubuntu $ubuntu"
    mkdir -p debs
    gh release download "$version" --repo obsproject/obs-studio --pattern "$deb" --dir debs --clobber
    if docker run --rm -v "$PWD:/w" -w /w "ubuntu:$ubuntu" bash -c '
        set -euo pipefail
        apt-get update -qq
        DEBIAN_FRONTEND=noninteractive apt-get install -y -qq "./debs/$1" > /dev/null
        # OBS finds its own libobs through the executable RPATH; outside the
        # obs process only the linker cache can, and OBS .debs install under
        # /usr/local without refreshing it.
        ldconfig
        # Installed exactly as the README says: extracted into $HOME.
        tar -xzf "$2" -C "$HOME"
        status=0
        # Each copy must sit where that OBS looks, under the module name OBS
        # derives from the file name, next to its data folder.
        for dir in "$HOME/.local/share/obs-studio/plugins/obs-playlist-deck" \
                   "$HOME/.config/obs-studio/plugins/obs-playlist-deck"; do
          so="$dir/obs-playlist-deck.so"
          [ -f "$so" ] || so="$dir/bin/64bit/obs-playlist-deck.so"
          if [ ! -f "$so" ] || [ ! -f "$dir/data/locale/en-US.ini" ]; then
            echo "missing: the plugin or its data under $dir"; status=1; continue
          fi
          out="$(ldd -r "$so" 2>&1)"
          if printf "%s\n" "$out" | grep -E "not found|undefined symbol"; then
            echo "unresolved imports in $so"; status=1
          else
            echo "ok: $so"
          fi
        done
        exit $status' _ "$deb" "$package"; then
      echo "::endgroup::"
      echo "ok: OBS $version on Ubuntu $ubuntu"
    else
      echo "::endgroup::"
      report "$kind" "the plugin does not load into OBS $version on Ubuntu $ubuntu (see the group above)"
    fi
    rm -f "debs/$deb"
  done
}

report() {
  if [ "$1" = required ]; then
    echo "::error::$2"
    failed_required=1
  else
    echo "::warning::$2 (prerelease: reported, not enforced)"
  fi
}

check_version "$required" required
[ -n "$optional" ] && [ "$optional" != "$required" ] && check_version "$optional" optional

exit "$failed_required"
