#!/usr/bin/env bash
#
# Prints the CHANGELOG section for a version, formatted for a GitHub release
# body.  Usage: release-notes.sh v1.2.4 [CHANGELOG.md]
#
# GitHub renders release bodies with soft line breaks promoted to hard ones, so
# the file's 80-column wrapping would come out as ragged mid-sentence breaks.
# Paragraphs and list items are therefore joined back into one line each; blank
# lines, headings, list starts, tables, block quotes and fenced code are left
# exactly as written.
set -euo pipefail

version="${1#v}"
changelog="${2:-CHANGELOG.md}"

# The section runs from its own "## [version]" heading to the next one, or to
# the link-reference block at the end of the file.
awk -v v="$version" '
  index($0, "## [" v "]") == 1 { found = 1; next }
  found && /^## \[/            { exit }
  found && /^\[[0-9]/          { exit }
  found                        { print }
' "$changelog" |
awk '
  function flush() { if (buf != "") { print buf; buf = "" } }
  /^[ \t]*```/                     { flush(); fence = !fence; print; next }
  fence                            { print; next }
  /^[ \t]*$/                       { flush(); print ""; next }
  /^#/                             { flush(); print; next }
  /^[ \t]*([-*+]|[0-9]+\.)[ \t]/   { flush(); buf = $0; next }
  /^[ \t]*[>|]/                    { flush(); print; next }
  { line = $0; sub(/^[ \t]+/, "", line); buf = (buf == "" ? line : buf " " line) }
  END { flush() }
' |
# Collapse runs of blank lines, then trim them from both ends.
awk 'BEGIN { blank = 1 } /^$/ { if (blank) next; blank = 1 } { if ($0 != "") blank = 0; print }' |
perl -0pe 's/\A\s+//; s/\s+\z/\n/'
