#!/usr/bin/env bash
# run.sh — run TrapezePDF unit tests on the host.
#
# These test portable C code (URL parsing, memmem, timegm, IFF
# clipboard payload parsing). OS4-specific code (Intuition, MuPDF
# integration) is NOT covered — that has to be tested on the guest.
#
# Requires: gcc or clang. Runs natively; no docker needed.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
CC="${CC:-cc}"
OUT="/tmp/trapeze-tests"

mkdir -p "$OUT"

echo "=== Building test binaries ==="
$CC -std=c99 -Wall -O0 -g -I "$REPO/src" \
    "$HERE/test_net.c" "$REPO/src/net.c" \
    -o "$OUT/test_net"
$CC -std=c99 -Wall -O0 -g -I "$REPO/src" \
    "$HERE/test_shims.c" \
    -o "$OUT/test_shims"
$CC -std=c99 -Wall -O0 -g \
    "$HERE/test_iff_clipboard.c" \
    -o "$OUT/test_iff_clipboard"

echo
echo "=== Running tests ==="
total_fail=0
for t in test_net test_shims test_iff_clipboard; do
    echo
    echo "--- $t ---"
    if "$OUT/$t"; then
        echo "  ✓ $t OK"
    else
        rc=$?
        echo "  ✗ $t failed (rc=$rc)"
        total_fail=$((total_fail + 1))
    fi
done

echo
if [ "$total_fail" -eq 0 ]; then
    echo "All test binaries passed."
else
    echo "$total_fail test binary(ies) failed."
    exit 1
fi
