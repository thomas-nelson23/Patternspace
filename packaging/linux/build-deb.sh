#!/usr/bin/env bash
# Builds patternspace_<version>_amd64.deb for Debian and Ubuntu: the app in /usr/bin with a menu
# entry, and the VST3, CLAP and LV2 plug-ins in /usr/lib/{vst3,clap,lv2}, where hosts look by
# default. Dependencies come from dpkg-shlibdeps, so the package installs on the distribution it
# was built on and newer ones.
#
#   packaging/linux/build-deb.sh <artefacts dir> <version> <output dir>
#
# <artefacts dir> holds one folder per format, like build/Patternspace_artefacts/Release.
set -euo pipefail

artefacts=$(cd "$1" && pwd)
version=$2
outdir=$3
arch=$(dpkg --print-architecture)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

pkg="$work/patternspace"
install -Dm755 "$artefacts/Standalone/Patternspace" "$pkg/usr/bin/patternspace"
mkdir -p "$pkg/usr/lib/vst3" "$pkg/usr/lib/clap" "$pkg/usr/lib/lv2"
cp -R "$artefacts/VST3/Patternspace.vst3" "$pkg/usr/lib/vst3/"
cp -R "$artefacts/LV2/Patternspace.lv2" "$pkg/usr/lib/lv2/"
install -Dm644 "$artefacts/CLAP/Patternspace.clap" "$pkg/usr/lib/clap/Patternspace.clap"
find "$pkg" -name "*.a" -delete

install -Dm644 /dev/stdin "$pkg/usr/share/applications/patternspace.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=Patternspace
GenericName=Drum sequencer
Comment=Generative, node-based drum sequencer and sample player
Exec=patternspace
Icon=audio-x-generic
Terminal=false
Categories=AudioVideo;Audio;Midi;Sequencer;
DESKTOP

# dpkg-shlibdeps wants to run from a source tree, so give it a minimal one.
mkdir -p "$work/src/debian"
printf 'Source: patternspace\n\nPackage: patternspace\nArchitecture: any\n' >"$work/src/debian/control"
binaries=$(find "$pkg" -type f \( -name "*.so" -o -path "*/usr/bin/*" \) -printf '%p ')
binaries+=" $pkg/usr/lib/clap/Patternspace.clap"
# shellcheck disable=SC2086
depends=$(cd "$work/src" && dpkg-shlibdeps -O -e $binaries 2>/dev/null | sed -n 's/^shlibs:Depends=//p')

mkdir -p "$pkg/DEBIAN"
cat >"$pkg/DEBIAN/control" <<CONTROL
Package: patternspace
Version: $version
Architecture: $arch
Maintainer: Patternspace <https://github.com/thomas-nelson23/Patternspace>
Depends: $depends
Section: sound
Priority: optional
Homepage: https://github.com/thomas-nelson23/Patternspace
Installed-Size: $(du -sk "$pkg/usr" | cut -f1)
Description: Generative, node-based drum sequencer and sample player
 Patch Euclidean, random, cellular and step generators through shapers into MIDI
 outs and sample players. Includes the standalone app and the VST3, CLAP and
 LV2 plug-ins.
CONTROL

mkdir -p "$outdir"
output="$outdir/patternspace_${version}_${arch}.deb"
dpkg-deb --root-owner-group --build "$pkg" "$output"
echo "Built $output"
