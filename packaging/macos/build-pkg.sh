#!/usr/bin/env bash
# Builds Patternspace-<version>-macOS.pkg: one installer for the app and the AU, VST3 and CLAP
# plug-ins, each a choice the user can untick under Customize.
#
#   packaging/macos/build-pkg.sh <artefacts dir> <version> <output.pkg>
#
# <artefacts dir> holds one folder per format, like build/Patternspace_artefacts/Release. The
# package is unsigned unless PKG_SIGN_IDENTITY names a "Developer ID Installer" identity in the
# keychain.
set -euo pipefail

artefacts=$(cd "$1" && pwd)
version=$2
output=$3
here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/pkgs"

# id, bundle in the artefacts folder, install location, title
components=(
  "app|Standalone/Patternspace.app|/Applications|Patternspace app"
  "au|AU/Patternspace.component|/Library/Audio/Plug-Ins/Components|Audio Unit (Logic, GarageBand, Ableton Live)"
  "aumidi|AU/Patternspace MIDI.component|/Library/Audio/Plug-Ins/Components|Audio Unit MIDI effect (Logic's MIDI FX slot)"
  "vst3|VST3/Patternspace.vst3|/Library/Audio/Plug-Ins/VST3|VST3 plug-in"
  "clap|CLAP/Patternspace.clap|/Library/Audio/Plug-Ins/CLAP|CLAP plug-in (Bitwig, Reaper)"
)

choices="" outline=""
for entry in "${components[@]}"; do
  IFS='|' read -r id bundle location title <<<"$entry"
  if [ ! -d "$artefacts/$bundle" ]; then
    # the MIDI effect is built beside the instrument; a build without it still packages
    if [ "$id" = "aumidi" ]; then echo "No $bundle, skipping" >&2; continue; fi
    echo "Missing $artefacts/$bundle" >&2; exit 1
  fi

  root="$work/root-$id"
  mkdir -p "$root"
  ditto "$artefacts/$bundle" "$root/$(basename "$bundle")"

  # Without this, Installer "relocates" a bundle to wherever it finds an older copy with the same
  # bundle id (a build folder, say) instead of the standard location.
  plist="$work/$id.plist"
  pkgbuild --analyze --root "$root" "$plist" >/dev/null
  i=0
  while plutil -extract "$i" xml1 -o /dev/null "$plist" 2>/dev/null; do
    plutil -replace "$i.BundleIsRelocatable" -bool NO "$plist"
    i=$((i + 1))
  done

  scripts=()
  if [ -d "$here/scripts-$id" ]; then scripts=(--scripts "$here/scripts-$id"); fi

  pkgbuild --root "$root" --component-plist "$plist" --install-location "$location" \
    --identifier "com.patternspace.patternspace.$id" --version "$version" \
    ${scripts[@]+"${scripts[@]}"} "$work/pkgs/$id.pkg"

  choices+="    <choice id=\"$id\" title=\"$title\" visible=\"true\" start_selected=\"true\">
        <pkg-ref id=\"com.patternspace.patternspace.$id\"/>
    </choice>
    <pkg-ref id=\"com.patternspace.patternspace.$id\" version=\"$version\" onConclusion=\"none\">$id.pkg</pkg-ref>
"
  outline+="        <line choice=\"$id\"/>
"
done

cat >"$work/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>Patternspace $version</title>
    <welcome file="welcome.html" mime-type="text/html"/>
    <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <domains enable_localSystem="true" enable_currentUserHome="false" enable_anywhere="false"/>
    <os-version min="11.0"/>
    <choices-outline>
$outline    </choices-outline>
$choices</installer-gui-script>
XML

mkdir -p "$work/resources"
cat >"$work/resources/welcome.html" <<HTML
<html><body style="font-family: -apple-system, sans-serif; font-size: 13px">
<p>This installs Patternspace $version:</p>
<ul>
<li>the Patternspace app in Applications</li>
<li>the Audio Unit, VST3 and CLAP plug-ins in /Library/Audio/Plug-Ins</li>
</ul>
<p>Click <b>Customize</b> on the Installation Type step to leave any of them out. If your DAW is
open, rescan its plug-ins (or restart it) afterwards.</p>
</body></html>
HTML

sign=()
if [ -n "${PKG_SIGN_IDENTITY:-}" ]; then sign=(--sign "$PKG_SIGN_IDENTITY"); fi

mkdir -p "$(dirname "$output")"
productbuild --distribution "$work/distribution.xml" --package-path "$work/pkgs" \
  --resources "$work/resources" ${sign[@]+"${sign[@]}"} "$output"
echo "Built $output"
