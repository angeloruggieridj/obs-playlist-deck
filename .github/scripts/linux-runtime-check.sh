#!/usr/bin/env bash
#
# Installs each Ubuntu .deb that OBS publishes for a release into a container
# of that Ubuntu, unpacks our package for the same Ubuntu over it, and asks the
# dynamic linker to resolve every library and symbol the plugin imports. Both
# copies of the plugin are checked: the OBS 33 layout and the legacy one.
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
        tar -xzf "$2" -C /
        status=0
        # Each copy must sit where that OBS looks, under the module name OBS
        # derives from the file name, next to a data folder of the same name.
        for pair in \
            /usr/lib/x86_64-linux-gnu/obs-modules/plugins/obs-playlist-deck.so:/usr/share/obs/obs-modules/plugins/obs-playlist-deck \
            /usr/lib/x86_64-linux-gnu/obs-plugins/obs-playlist-deck.so:/usr/share/obs/obs-plugins/obs-playlist-deck; do
          so="${pair%%:*}" data="${pair#*:}"
          if [ ! -f "$so" ] || [ ! -f "$data/locale/en-US.ini" ]; then
            echo "missing: $so or $data/locale/en-US.ini"; status=1; continue
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
