#!/bin/zsh
# Build Rave It Out for Apple Silicon Macs, all in one command:
#
#   ./build-mac.sh
#
# The only thing you need installed first is Apple's free Command Line Tools
# (this script offers to install them). Everything else (CMake, FFmpeg's
# source, libusb) is downloaded from its official site, checked against a
# known checksum, and kept inside the Build/ and extern/ folders. Nothing is
# installed system-wide. Homebrew is NOT required.
#
# The first build takes roughly 10-20 minutes; later ones are much faster.
# When it finishes, StepMania.app is in this folder. Double-click it to play.
set -euo pipefail

SM_DIR=${0:A:h}
cd "$SM_DIR"
BUILD_DIR=$SM_DIR/Build/arm64
TOOLS=$SM_DIR/Build/tools
JOBS=$(sysctl -n hw.ncpu)

CMAKE_VER=4.5.1
CMAKE_URL=https://github.com/Kitware/CMake/releases/download/v$CMAKE_VER/cmake-$CMAKE_VER-macos-universal.tar.gz
CMAKE_SHA=e1fb288543e4eb2a410e385ecebc64323bda51f0205782f2b094ccd5d572ca2d
FFMPEG_VER=5.1.10
FFMPEG_URL=https://ffmpeg.org/releases/ffmpeg-$FFMPEG_VER.tar.xz
FFMPEG_SHA=392306d6fc45dab0e9e0ea55381e071842e83a2fb31d320aeda40477a7766293
FFMPEG_DIR=$SM_DIR/extern/ffmpeg-5.1

step() { print -P "\n%F{cyan}==> $1%f"; }
fail() { print -P "\n%F{red}Error:%f $1" >&2; exit 1; }

fetch() { # url sha dest
  if [[ ! -f $3 ]]; then
    curl -fL --retry 3 --progress-bar -o "$3.part" "$1" || fail "Download failed: $1
Check your internet connection and run ./build-mac.sh again."
    mv "$3.part" "$3"
  fi
  if ! echo "$2  $3" | shasum -a 256 -c - >/dev/null 2>&1; then
    rm -f "$3"
    fail "The download $3:t didn't match its expected checksum, so it was deleted.
Run ./build-mac.sh again. If it keeps happening, please report it."
  fi
}

# --- 1. Check the Mac ------------------------------------------------------
step "Checking your Mac"
[[ $(uname -m) == arm64 ]] ||
  fail "This script builds for Apple Silicon (M1 or newer). Intel Macs aren't supported yet."

if ! xcode-select -p >/dev/null 2>&1 || ! xcrun --find clang >/dev/null 2>&1; then
  print "Apple's Command Line Tools are needed to compile. A window will open to install them."
  xcode-select --install 2>/dev/null || true
  fail "Click Install in the window that opened. When it finishes, run ./build-mac.sh again."
fi
print "Apple Silicon and Command Line Tools: OK"

# --- 2. CMake ---------------------------------------------------------------
step "Getting CMake $CMAKE_VER"
CMAKE=$TOOLS/cmake-$CMAKE_VER-macos-universal/CMake.app/Contents/bin/cmake
if [[ ! -x $CMAKE ]]; then
  mkdir -p "$TOOLS"
  fetch "$CMAKE_URL" "$CMAKE_SHA" "$TOOLS/cmake-$CMAKE_VER.tar.gz"
  tar -xzf "$TOOLS/cmake-$CMAKE_VER.tar.gz" -C "$TOOLS"
  xattr -dr com.apple.quarantine "$TOOLS/cmake-$CMAKE_VER-macos-universal" 2>/dev/null || true
fi
"$CMAKE" --version | head -1

# --- 3. FFmpeg source (plays the background videos) -------------------------
step "Getting FFmpeg $FFMPEG_VER source"
if [[ ! -x $FFMPEG_DIR/configure ]]; then
  mkdir -p "$SM_DIR/Build/downloads"
  fetch "$FFMPEG_URL" "$FFMPEG_SHA" "$SM_DIR/Build/downloads/ffmpeg-$FFMPEG_VER.tar.xz"
  rm -rf "$FFMPEG_DIR" "$SM_DIR/extern/ffmpeg-$FFMPEG_VER"
  tar -xJf "$SM_DIR/Build/downloads/ffmpeg-$FFMPEG_VER.tar.xz" -C "$SM_DIR/extern"
  mv "$SM_DIR/extern/ffmpeg-$FFMPEG_VER" "$FFMPEG_DIR"
fi
print "FFmpeg source: OK"

# --- 4. libusb (dance pad / cabinet USB support) ----------------------------
step "Building libusb (first time only)"
LIBUSB_PREFIX=$("$SM_DIR/Utils/macos-bundle-libusb.sh" --build-only | tail -1)
[[ -f $LIBUSB_PREFIX/lib/libusb-0.1.4.dylib ]] || fail "libusb didn't build."
print "libusb: OK"

# --- 5. Configure + compile the game ----------------------------------------
step "Configuring"
# Point CMake at our libusb (not any Homebrew copy), whether or not
# pkg-config is installed.
export PKG_CONFIG_LIBDIR=$LIBUSB_PREFIX/lib/pkgconfig PKG_CONFIG_PATH=
export LibUSB_ROOT_DIR=$LIBUSB_PREFIX
# CMake searches Homebrew's folders on its own, so tell it to skip them:
# the build should come out the same with or without Homebrew installed.
"$CMAKE" -S "$SM_DIR" -B "$BUILD_DIR" -G "Unix Makefiles" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 \
  "-DCMAKE_IGNORE_PREFIX_PATH=/opt/homebrew;/usr/local" \
  -Dusb_LIBRARY="$LIBUSB_PREFIX/lib/libusb.dylib" \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DWITH_FFMPEG_JOBS="$JOBS" \
  -Wno-dev >"$SM_DIR/Build/configure.log" 2>&1 ||
  fail "Configuring failed. Details are in Build/configure.log"
print "Configured. Details are in Build/configure.log"

step "Compiling (this is the long part; the first time can take 10-20 minutes)"
"$CMAKE" --build "$BUILD_DIR" -j"$JOBS" >"$SM_DIR/Build/compile.log" 2>&1 || {
  grep -E "error:|Error [0-9]" "$SM_DIR/Build/compile.log" | head -20 >&2
  fail "Compiling failed. The full log is in Build/compile.log"
}
print "Compiled."

# --- 6. Make the app self-contained and sign it -----------------------------
step "Finishing StepMania.app"
"$SM_DIR/Utils/macos-bundle-libusb.sh" "$SM_DIR/StepMania.app"
codesign --force --deep --sign - "$SM_DIR/StepMania.app" 2>/dev/null
codesign --verify --deep --strict "$SM_DIR/StepMania.app" || fail "Signing check failed."
# Portable mode: keep Songs, Save and Logs in this folder instead of
# ~/Library/Application Support.
[[ -e $SM_DIR/portable.ini ]] || : > "$SM_DIR/portable.ini"

print -P "\n%F{green}Done!%f StepMania.app is ready in:\n  $SM_DIR\n"
print "Double-click StepMania.app to play. Put your songs in the Songs folder next to it."
print "The first time it opens, macOS may ask for Input Monitoring permission; allow it so"
print "keyboards and dance pads work."
