#!/usr/bin/env bash
# Build Munchi Delay for Move (aarch64). Uses Docker unless CROSS_PREFIX is set.
#
#   ./scripts/build.sh   -> dist/munchi-delay/ and dist/munchi-delay-module.tar.gz
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
IMAGE_NAME="munchi-builder"
cd "$REPO_ROOT"

if [ -z "${CROSS_PREFIX:-}" ] && [ ! -f "/.dockerenv" ]; then
    if ! docker image inspect "$IMAGE_NAME" &>/dev/null; then
        docker build -t "$IMAGE_NAME" -f scripts/Dockerfile .
    fi
    docker run --rm -v "$REPO_ROOT:/build" -u "$(id -u):$(id -g)" -w /build \
        "$IMAGE_NAME" ./scripts/build.sh
    exit 0
fi

CROSS_PREFIX="${CROSS_PREFIX:-aarch64-linux-gnu-}"
MODULE_DIR="dist/munchi-delay"
TARBALL="dist/munchi-delay-module.tar.gz"
mkdir -p build dist
rm -rf "$MODULE_DIR"
mkdir -p "$MODULE_DIR"

echo "Compiling..."
# An audio FX loads as modules/audio_fx/<id>/<id>.so. -fno-gnu-unique and
# --exclude-libs,ALL: no STB_GNU_UNIQUE symbols, so a new build loads without
# restarting Move; --no-undefined: a missing symbol fails here, not at dlopen.
${CROSS_PREFIX}g++ -O2 -g -std=c++17 -shared -fPIC -fvisibility=hidden -fno-gnu-unique \
    -Wall -Wno-vla -Wno-unused-variable -Wno-unused-but-set-variable -Wno-class-memaccess \
    -Wno-sign-compare -Wno-parentheses \
    -Isrc/dsp -Isrc/dsp/tempo/shim -Isrc/dsp/tempo \
    src/dsp/munchi_delay.cpp \
    -o build/munchi-delay.so \
    -static-libstdc++ -static-libgcc -Wl,--exclude-libs,ALL -Wl,--no-undefined -lpthread -lm

cat src/module.json > "$MODULE_DIR/module.json"
cat build/munchi-delay.so > "$MODULE_DIR/munchi-delay.so"
cat src/help.json > "$MODULE_DIR/help.json"
cat README.md > "$MODULE_DIR/README.md"
cat LICENSE > "$MODULE_DIR/LICENSE"
cat THIRD_PARTY.md > "$MODULE_DIR/THIRD_PARTY.md"

rm -f "$TARBALL"
( cd dist && tar -czf "$(basename "$TARBALL")" munchi-delay )
echo "Output: $MODULE_DIR/  Tarball: $TARBALL ($(du -h "$TARBALL" | cut -f1))"
