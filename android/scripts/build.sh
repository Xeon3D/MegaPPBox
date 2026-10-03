#!/usr/bin/env bash
# MegaPPBox for Android - build the native library and the app (Linux or WSL).
#
#   bash android/scripts/build.sh [configure] [release]
#                       (toolchain: setup-toolchain.sh; libraries: build-deps.sh)
#
# 1. libMegaPPBox.so: this repository's emulator (CMake, with if(ANDROID) in place of
#    the Qt front end) + android/native (the platform layer and the JNI bridge), with
#    the NDK's CMake toolchain, in $T/build-native; copied to app/src/main/jniLibs.
# 2. The app (android/app, Kotlin, Gradle): android/out/MegaPPBox-debug.apk, or with
#    "release" android/out/MegaPPBox-<version>-android.apk - not debuggable, signed
#    with the release key ($T/release-signing.properties; see app/build.gradle.kts).
#
# The version is MegaPPBox's own (MEGAPPBOX_VERSION in CMakeLists.txt).
set -euo pipefail
CONFIGURE=0 RELEASE=0
for a in "$@"; do
    case "$a" in
        configure) CONFIGURE=1 ;;
        release) RELEASE=1 ;;
        *) echo "usage: build.sh [configure] [release]" >&2; exit 2 ;;
    esac
done
HERE=$(cd "$(dirname "$0")/.." && pwd)   # android/
ROOT=$(cd "$HERE/.." && pwd)             # the repository
. "$HERE/scripts/env.sh"
B=$T/build-native
export PKG_CONFIG_LIBDIR=$T/deps/lib/pkgconfig PKG_CONFIG_PATH=
# The build-deps.sh headers, for sources that include them without linking the
# library's target (the desktop builds find them in /usr/include).
DEPS_INC="-isystem $T/deps/include -isystem $T/deps/include/freetype2"

# 1. The library.  A build directory configured for another source tree starts over.
if [ -f "$B/CMakeCache.txt" ] && ! grep -q "CMAKE_HOME_DIRECTORY:INTERNAL=$ROOT\$" "$B/CMakeCache.txt"; then
    rm -rf "$B"
fi
if [ $CONFIGURE = 1 ] || [ ! -f "$B/build.ninja" ]; then
    cmake -S "$ROOT" -B "$B" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_FIND_ROOT_PATH="$T/deps" -DCMAKE_PREFIX_PATH="$T/deps" \
        -DCMAKE_C_FLAGS="$DEPS_INC" -DCMAKE_CXX_FLAGS="$DEPS_INC" \
        -DQT=OFF -DSTATIC_BUILD=OFF -DDYNAREC=ON -DOPENAL=ON
fi
cmake --build "$B" --target 86Box
mkdir -p "$HERE/app/src/main/jniLibs/arm64-v8a"
cp "$B/src/libMegaPPBox.so" "$HERE/app/src/main/jniLibs/arm64-v8a/"

# 2. The app.
mkdir -p "$HERE/out"
if [ $RELEASE = 1 ]; then
    [ -f "$T/release-signing.properties" ] || { echo "no release key ($T/release-signing.properties)" >&2; exit 1; }
    (cd "$HERE" && ./gradlew --no-daemon -q assembleRelease)
    ver=$(grep -o 'MEGAPPBOX_VERSION "[^"]*"' "$ROOT/CMakeLists.txt" | cut -d'"' -f2)
    cp "$HERE/app/build/outputs/apk/release/app-release.apk" "$HERE/out/MegaPPBox-$ver-android.apk"
else
    (cd "$HERE" && ./gradlew --no-daemon -q assembleDebug)
    cp "$HERE/app/build/outputs/apk/debug/app-debug.apk" "$HERE/out/MegaPPBox-debug.apk"
fi
ls -la "$HERE/out/"
