#!/bin/bash
# Copy a local build to the Move. Signal Chain picks up a new .so the next
# time a slot loads the effect.
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$(dirname "$SCRIPT_DIR")"
HOST="${MOVE_HOST:-ableton@move.local}"
DEST=/data/UserData/schwung/modules/audio_fx/munchi-delay

[ -d dist/munchi-delay ] || { echo "Run ./scripts/build.sh first."; exit 1; }
ssh "$HOST" "mkdir -p $DEST"
scp dist/munchi-delay/module.json dist/munchi-delay/help.json dist/munchi-delay/README.md \
    dist/munchi-delay/LICENSE dist/munchi-delay/THIRD_PARTY.md "$HOST:$DEST/"
scp dist/munchi-delay/munchi-delay.so "$HOST:$DEST/munchi-delay.so.new"
ssh "$HOST" "mv -f $DEST/munchi-delay.so.new $DEST/munchi-delay.so"
echo "Installed to $DEST. Add Munchi Delay to a slot's or the Master FX chain."
