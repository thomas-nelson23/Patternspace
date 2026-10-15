#!/usr/bin/env bash
# Prints the CHANGELOG.md section for one version, for the GitHub release body:
#
#   packaging/release-notes.sh 0.3.0
#
# A version with no section (or an "Unreleased" one only) prints a one-line note instead, so a
# release never fails for want of notes.
set -euo pipefail
version=$1
changelog=$(cd "$(dirname "$0")/.." && pwd)/CHANGELOG.md
notes=$(awk -v v="$version" '
  /^## / { on = ($2 == v || $2 == "[" v "]"); next }
  on { print }
' "$changelog")
if [ -z "$(echo "$notes" | tr -d '[:space:]')" ]; then
  echo "Patternspace $version. See CHANGELOG.md for what changed."
else
  echo "$notes"
fi
