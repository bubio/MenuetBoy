#!/bin/sh
# Run the emulator core against test ROMs on the host (no VM needed).
#
# Usage: scripts/test.sh [rom ...]   (default: tests/roms/*.gb, screen, tone and RTC tests)
#
# Requires: brew install blink
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
TOOLS=$ROOT/.tools
BUILD=$ROOT/build
FASM=$TOOLS/fasm/fasm.x64
FASM_VERSION=1.73.35

command -v blink >/dev/null || { echo "missing: blink (brew install blink)" >&2; exit 1; }
if [ ! -x "$FASM" ]; then
    mkdir -p "$TOOLS"
    curl -sSLf "https://flatassembler.net/fasm-$FASM_VERSION.tgz" | tar xz -C "$TOOLS"
fi
mkdir -p "$BUILD" "$TOOLS/blinkroot"

export BLINK_PREFIX="$TOOLS/blinkroot"
(cd "$ROOT/tests" && blink -m "/SystemRoot$FASM" -m 262144 harness.asm "/SystemRoot$BUILD/harness" >/dev/null)

pass=0
fail=0

# Screen tests: <rom> <frames> <reference png>, run only without arguments
if [ $# -eq 0 ]; then
    for test in "$ROOT/roms/dmg-acid2.gb 120 dmg-acid2" "$ROOT/tests/screen/halt_bug.gb 300 halt_bug"; do
        set -- $test
        printf '== %s (screen)\n' "$3"
        blink -m "/SystemRoot$BUILD/harness" "/SystemRoot$1" "$2" \
            "/SystemRoot$BUILD/$3.pgm" >/dev/null || true
        if python3 "$ROOT/tests/compare_screen.py" "$ROOT/tests/ref/$3.png" "$BUILD/$3.pgm"; then
            pass=$((pass + 1))
        else
            fail=$((fail + 1))
        fi
    done
    set --

    printf '== 440Hz tone (sound)\n'
    python3 "$ROOT/tests/sound_tone.py" make "$BUILD/tone.gb"
    blink -m "/SystemRoot$BUILD/harness" "/SystemRoot$BUILD/tone.gb" 60 \
        "/SystemRoot$BUILD/tone.pgm" "/SystemRoot$BUILD/tone.raw" >/dev/null || true
    if python3 "$ROOT/tests/sound_tone.py" check "$BUILD/tone.raw"; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
    fi

    printf '== MBC3 RTC\n'
    python3 "$ROOT/tests/rtc_test.py" make "$BUILD/rtc.gb"
    if blink -m "/SystemRoot$BUILD/harness" "/SystemRoot$BUILD/rtc.gb" 600; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
    fi
fi

[ $# -gt 0 ] || set -- "$ROOT"/tests/roms/*.gb
for rom in "$@"; do
    rom=$(cd "$(dirname "$rom")" && pwd)/$(basename "$rom")
    printf '== %s\n' "$(basename "$rom")"
    if blink -m "/SystemRoot$BUILD/harness" "/SystemRoot$rom"; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
    fi
done
echo "passed: $pass  failed: $fail"
[ "$fail" -eq 0 ]
