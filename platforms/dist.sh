#!/bin/sh
# dist.sh - builds every platform whose toolchain is installed and packages
# each as dist/aitd-<platform>-<version>.zip, with the README, the licence
# and the changelog. Platforms without a toolchain are skipped and listed.
#
#   platforms/dist.sh [platform...]     (default: all)

set -u
cd "$(dirname "$0")/.."
VERSION=$(cat VERSION)
DIST=dist
STAGE=build/dist
ALL="macos linux windows win9x dos web psp vita ios"
PLATFORMS=${*:-$ALL}

mkdir -p "$DIST"
built=""
skipped=""

# package NAME FILE... : zips the files flat, with the documents.
package() {
    name=$1
    shift
    rm -rf "${STAGE:?}/$name"
    mkdir -p "$STAGE/$name"
    for f in "$@"; do
        cp -R "$f" "$STAGE/$name/"
    done
    cp README.md LICENSE CHANGELOG.md "$STAGE/$name/"
    if [ -f "platforms/$name/README.md" ]; then
        cp "platforms/$name/README.md" "$STAGE/$name/README-$name.md"
    fi
    rm -f "$DIST/aitd-$name-$VERSION.zip"
    (cd "$STAGE/$name" && zip -qr "../../../$DIST/aitd-$name-$VERSION.zip" .)
    built="$built $name"
}

try() {
    name=$1
    shift
    if "$@" > "build/dist-$name.log" 2>&1; then
        return 0
    fi
    skipped="$skipped $name"
    return 1
}

mkdir -p build
for p in $PLATFORMS; do
    case $p in
    macos)
        [ "$(uname -s)" = Darwin ] && try macos make && package macos aitd ;;
    linux)
        try linux make linux && package linux build/linux/aitd ;;
    windows)
        try windows make windows && package windows build/windows/aitd.exe ;;
    win9x)
        try win9x make win9x && package win9x build/win9x/aitd.exe ;;
    dos)
        try dos make dos && package dos build/dos/aitd.exe build/dos/dos32a.exe ;;
    web)
        try web make web && package web build/web/index.html build/web/aitd.js build/web/aitd.wasm ;;
    psp)
        try psp make psp && package psp $(ls -d build/psp/*/) ;;
    vita)
        try vita make vita && package vita build/vita/*.vpk ;;
    ios)
        [ "$(uname -s)" = Darwin ] && try ios make ios && package ios build/ios/aitd.ipa ;;
    *)
        echo "unknown platform $p" >&2 ;;
    esac
done

echo "version $VERSION"
echo "built:  ${built:- none}"
[ -n "$skipped" ] && echo "failed:${skipped} (see build/dist-<platform>.log)"
ls -la "$DIST"
