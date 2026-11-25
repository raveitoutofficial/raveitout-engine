#!/bin/zsh
# Bundle libusb into StepMania.app so it runs without Homebrew.
#
#   Utils/macos-bundle-libusb.sh [path/to/StepMania.app]
#
# The PIUIO driver links libusb-compat (libusb 0.1 API), which in turn needs
# libusb 1.0. Homebrew's bottles are built for the newest macOS only, so this
# builds both from the official release tarballs for MACOSX_DEPLOYMENT_TARGET
# (default 11.0, same as the app), once, into Build/libusb-macos. Then it
# copies the two dylibs into Contents/Frameworks and points the executable at
# them via @executable_path. Run it after every build (linking resets the
# path) and before code signing.
#
# Both libraries are LGPL-2.1-or-later. They stay separate dylibs (never
# statically linked) and their license texts are copied next to them.
set -euo pipefail

SM_DIR=${0:A:h:h}
APP=${1:-$SM_DIR/StepMania.app}
EXE=$APP/Contents/MacOS/StepMania
FW=$APP/Contents/Frameworks
export MACOSX_DEPLOYMENT_TARGET=${MACOSX_DEPLOYMENT_TARGET:-11.0}

WORK=$SM_DIR/Build/libusb-macos
PREFIX=$WORK/prefix-$MACOSX_DEPLOYMENT_TARGET
LIBUSB_VER=1.0.30
LIBUSB_URL=https://github.com/libusb/libusb/releases/download/v$LIBUSB_VER/libusb-$LIBUSB_VER.tar.bz2
LIBUSB_SHA=fea36f34f9156400209595e300840767ab1a385ede1dc7ee893015aea9c6dbaf
COMPAT_VER=0.1.9
COMPAT_URL=https://downloads.sourceforge.net/project/libusb/libusb-compat-0.1/libusb-compat-$COMPAT_VER/libusb-compat-$COMPAT_VER.tar.bz2
COMPAT_SHA=179d934676ad3bd172a5a37cf5c6351b8e9d7816feb3029252e79b30a518a3be

fetch() { # url sha dest
  if [[ ! -f $3 ]]; then
    curl -fsSL --retry 3 -o "$3.part" "$1"
    mv "$3.part" "$3"
  fi
  echo "$2  $3" | shasum -a 256 -c - >/dev/null || { echo "checksum mismatch: $3" >&2; rm -f "$3"; exit 1; }
}

build_libs() {
  mkdir -p "$WORK/dl" "$WORK/src"
  fetch "$LIBUSB_URL" "$LIBUSB_SHA" "$WORK/dl/libusb-$LIBUSB_VER.tar.bz2"
  fetch "$COMPAT_URL" "$COMPAT_SHA" "$WORK/dl/libusb-compat-$COMPAT_VER.tar.bz2"
  local jobs; jobs=$(sysctl -n hw.ncpu)
  local flags="-arch arm64 -mmacosx-version-min=$MACOSX_DEPLOYMENT_TARGET -O2"

  rm -rf "$WORK/src/libusb-$LIBUSB_VER" "$WORK/src/libusb-compat-$COMPAT_VER" "$PREFIX"
  tar -xjf "$WORK/dl/libusb-$LIBUSB_VER.tar.bz2" -C "$WORK/src"
  tar -xjf "$WORK/dl/libusb-compat-$COMPAT_VER.tar.bz2" -C "$WORK/src"

  ( cd "$WORK/src/libusb-$LIBUSB_VER"
    CFLAGS=$flags LDFLAGS=$flags ./configure --prefix="$PREFIX" \
      --disable-static --disable-dependency-tracking >/dev/null
    make -j"$jobs" >/dev/null && make install >/dev/null )

  # Find only our libusb-1.0, never Homebrew's.
  ( cd "$WORK/src/libusb-compat-$COMPAT_VER"
    PKG_CONFIG_LIBDIR=$PREFIX/lib/pkgconfig PKG_CONFIG_PATH= \
    CFLAGS=$flags LDFLAGS=$flags ./configure --prefix="$PREFIX" \
      --disable-static --disable-dependency-tracking >/dev/null
    make -j"$jobs" >/dev/null && make install >/dev/null )

  mkdir -p "$PREFIX/licenses"
  cp "$WORK/src/libusb-$LIBUSB_VER/COPYING" "$PREFIX/licenses/libusb-$LIBUSB_VER-COPYING.txt"
  cp "$WORK/src/libusb-compat-$COMPAT_VER/COPYING" "$PREFIX/licenses/libusb-compat-$COMPAT_VER-COPYING.txt"
}

[[ -x $EXE ]] || { echo "no executable at $EXE" >&2; exit 1; }
[[ -f $PREFIX/lib/libusb-0.1.4.dylib && -f $PREFIX/lib/libusb-1.0.0.dylib ]] || build_libs

# Whatever libusb-0.1 the linker picked up (Homebrew, /usr/local, ...).
OLD=$(otool -L "$EXE" | awk '/libusb-0\.1\.4\.dylib/ && $1 !~ /^@/ {print $1}')

mkdir -p "$FW"
for lib in libusb-0.1.4.dylib libusb-1.0.0.dylib; do
  rm -f "$FW/$lib"
  cp "$PREFIX/lib/$lib" "$FW/$lib"
  chmod u+w "$FW/$lib"
  install_name_tool -id "@rpath/$lib" "$FW/$lib" 2>/dev/null
done
install_name_tool -change "$PREFIX/lib/libusb-1.0.0.dylib" "@rpath/libusb-1.0.0.dylib" \
  "$FW/libusb-0.1.4.dylib" 2>/dev/null
install_name_tool -add_rpath "@loader_path" "$FW/libusb-0.1.4.dylib" 2>/dev/null || true

if [[ -n $OLD ]]; then
  install_name_tool -change "$OLD" "@rpath/libusb-0.1.4.dylib" "$EXE" 2>/dev/null
fi
otool -l "$EXE" | grep -q "@executable_path/../Frameworks" ||
  install_name_tool -add_rpath "@executable_path/../Frameworks" "$EXE" 2>/dev/null

mkdir -p "$APP/Contents/Resources/ThirdPartyLicenses"
cp "$PREFIX/licenses/"* "$APP/Contents/Resources/ThirdPartyLicenses/"

# Fail loudly if anything still points outside the system or the bundle.
leak=$( { otool -L "$EXE"; otool -L "$FW"/*.dylib; } | awk 'NR>0 && /^\t/ {print $1}' |
  grep -Ev '^(/System/|/usr/lib/|@rpath/|@executable_path/|@loader_path/)' || true)
if [[ -n $leak ]]; then
  echo "non-bundled libraries still linked:" >&2; echo "$leak" >&2; exit 1
fi
echo "Bundled libusb $LIBUSB_VER + libusb-compat $COMPAT_VER (macOS $MACOSX_DEPLOYMENT_TARGET+) into $FW"
