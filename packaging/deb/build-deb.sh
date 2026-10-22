#!/bin/sh
# Builds the Debian package from the AppDir linuxdeploy fills for the
# AppImage. Only Qt and its ICU come along, in /opt/qure-data: distributions
# ship a Qt too old for Qure Data. Every other library comes from the system
# and becomes a dependency, which dpkg-shlibdeps works out, so this has to
# run on the oldest system the package is for. The C++ runtime is linked
# statically (QURE_STATIC_LIBSTDCXX), since that system's is too old.
#
# Usage: build-deb.sh <AppDir> <version> <output.deb>
set -eu

if [ $# -ne 3 ]; then
    echo "Usage: $0 <AppDir> <version> <output.deb>" >&2
    exit 2
fi
appdir=$1
version=$2
output=$3
logo=$(cd "$(dirname "$0")/../../src/app/logo" && pwd)

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
# Under debian/ dpkg-shlibdeps takes the libraries in it as the package's own
root=$work/debian/qure-data
prefix=$root/opt/qure-data

mkdir -p "$prefix/lib" "$root/usr/bin" "$root/DEBIAN"
cp -a "$appdir/usr/bin" "$appdir/usr/plugins" "$appdir/usr/qml" "$prefix/"
if [ -d "$appdir/usr/translations" ]; then
    cp -a "$appdir/usr/translations" "$prefix/"
fi
cp -a "$appdir"/usr/lib/libQt6* "$appdir"/usr/lib/libicu* "$prefix/lib/"
ln -s /opt/qure-data/bin/qure "$root/usr/bin/qure"
install -Dm644 "$logo/qure.desktop" "$root/usr/share/applications/qure.desktop"
install -Dm644 "$logo/qure.svg" "$root/usr/share/icons/hicolor/scalable/apps/qure.svg"
# The .qd file type; shared-mime-info's trigger updates the database
install -Dm644 "$logo/qure-data.xml" "$root/usr/share/mime/packages/qure-data.xml"
chmod -R u+rwX,go+rX,go-w "$root"

printf 'Source: qure-data\n\nPackage: qure-data\nArchitecture: amd64\n' > "$work/debian/control"
# Fail rather than build a package without dependencies
if ! shlibs=$(cd "$work" && find debian/qure-data/opt -type f \( -name '*.so*' -o -path '*/bin/qure' \) \
    -exec dpkg-shlibdeps -O -l"$prefix/lib" {} +); then
    echo "dpkg-shlibdeps failed" >&2
    exit 1
fi
libs=$(printf '%s\n' "$shlibs" | sed -n 's/^shlibs:Depends=//p')
if [ -z "$libs" ]; then
    echo "No dependencies found" >&2
    exit 1
fi

# The audio backends are loaded at runtime (miniaudio), so dpkg-shlibdeps
# can't see them: recommend the common ones.
cat > "$root/DEBIAN/control" <<EOF2
Package: qure-data
Version: $version
Architecture: amd64
Maintainer: Henning Gruendl <henning.gruendl@qt.io>
Installed-Size: $(du -sk "$root" | cut -f1)
Depends: $libs
Recommends: libasound2 | libasound2t64, libpulse0 | libjack-jackd2-0
Section: sound
Priority: optional
Homepage: https://github.com/iam-peter/qure-data
Description: Visual programming for audio, compatible with Pure Data
 Qure Data opens, edits and saves Pure Data patches: boxes of audio and
 control objects connected by patch cords. Its engine never locks the
 audio thread, its canvas is drawn on the GPU, and its GUI nodes are QML
 components, so new ones can be written without compiling anything.
EOF2

dpkg-deb --root-owner-group -Zxz -b "$root" "$output"
