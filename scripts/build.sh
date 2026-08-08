#!/usr/bin/env bash
# build.sh — cross-compile libmupdf.a (+ static thirdparty deps) for
# AmigaOS 4 PPC, then link our viewer against it.
#
# Runs inside the pdfview-os4-build:local docker image (walkero OS4
# toolchain + native tools). Output goes to build/.
#
# Usage:
#   ./scripts/build.sh           — build viewer + libmupdf
#   ./scripts/build.sh mupdf     — build libmupdf only
#   ./scripts/build.sh clean     — nuke build tree
#   ./scripts/build.sh shell     — drop into a shell inside docker
set -euo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE="${IMAGE:-pdfview-os4-build:local}"

case "${1:-all}" in
  clean)
    rm -rf "$HERE/build"
    rm -rf "$HERE/mupdf/build"
    exit 0
    ;;
  shell)
    exec docker run --rm -it -v "$HERE:/work" -w /work "$IMAGE" bash
    ;;
esac

MODE="${1:-all}"

# The build script that runs inside docker. Written into a heredoc so
# the surrounding bash env doesn't have to survive the docker boundary.
docker run --rm -v "$HERE:/work" -w /work "$IMAGE" bash -c '
set -euo pipefail
export PATH=/opt/ppc-amigaos/bin:$PATH
MODE="'"$MODE"'"

# --- Cross toolchain ---
export CC=ppc-amigaos-gcc
export CXX=ppc-amigaos-g++
export AR=ppc-amigaos-ar
export RANLIB=ppc-amigaos-ranlib
export STRIP=ppc-amigaos-strip

# --- Base CFLAGS matching our python-amigaos4 flags ---
# -mcpu=750 baseline (PPC G3/G4 era, works on all sam460ex-family)
# -mhard-float; big-endian; no altivec so it runs on plain G3 too
BASE_CFLAGS="-mcrt=newlib -mhard-float -O2 -mcpu=750 -mno-altivec \
    -mno-powerpc64 -Wall -D__PPC__ -D__USE_INLINE__ -D__USE_OLD_TIMEVAL__ \
    -DAMIGA -D_AMIGA -Dpowerpc -DSSIZE_MAX=0x7fffffff \
    -Du_int8_t=uint8_t -Du_int16_t=uint16_t \
    -Du_int32_t=uint32_t -Du_int64_t=uint64_t \
    -I/work/src/compat"    # memory.h shim for lcms2 (legacy header)

# --- Build MuPDF as static library ---
# We disable features we do NOT need for a viewer:
#   mujs=no      — JavaScript in PDFs (rare; +size)
#   html=no      — full HTML/EPUB rendering path (we only care about PDF)
#   extract=no   — DOCX/HTML output (we output to screen, not files)
#   xps=no       — Microsoft XPS format
#   svg=no       — standalone SVG viewer
#   barcode=no   — barcode rendering (not needed for PDF viewing)
#
# Cross-compile setup: HAVE_X11=no + tell MuPDF to skip GLUT/GL viewers.
# We only want libmupdf.a + libmupdf-third.a.
echo "=== stage: build libmupdf ($MODE) ==="
cd /work/mupdf

# Force big-endian in the third-party JPEG library (bundled jpeg
# auto-detects endian but sometimes gets it wrong on cross-compile)
export CFLAGS="$BASE_CFLAGS -DFZ_BIG_ENDIAN=1"
export XCFLAGS="$CFLAGS"

# Skip the CMap and Font hexdump generation? No — MuPDF regenerates
# these headers at build time and needs a NATIVE compiler for the
# hexdump step. hexdump.sh in scripts/ is a shell script wrapper
# around xxd — works fine even in cross-compile because the output
# is C source, not binary.
#
# Build just the libs, not the viewer or CLI tools.
# `make libs` builds:
#   build/release/libmupdf.a
#   build/release/libmupdf-third.a
make -j$(nproc 2>/dev/null || echo 2) \
    build=release \
    OS=amigaos \
    HAVE_X11=no HAVE_GLUT=no HAVE_LIBCRYPTO=no HAVE_CURL=no \
    HAVE_LEPTONICA=no HAVE_TESSERACT=no HAVE_ZXINGCPP=no \
    js=no \
    barcode=no \
    tesseract=no \
    libs 2>&1 | tail -50 || {
    echo "=== mupdf build FAILED ==="
    exit 1
}

echo "=== libmupdf artifacts ==="
ls -la build/release/libmupdf.a build/release/libmupdf-third.a 2>&1 | head -5

if [ "$MODE" = "mupdf" ]; then
    exit 0
fi

# --- Compile our viewer ---
echo "=== stage: build pdfview ==="
mkdir -p /work/build
cd /work
export CFLAGS="$BASE_CFLAGS -Imupdf/include"
export LDFLAGS="-mcrt=newlib -mcpu=750 -mno-altivec -mno-powerpc64 -athread=native"
export LIBS="-Lmupdf/build/release -lmupdf -lmupdf-third -lpthread -lm -lauto"

if ls src/*.c >/dev/null 2>&1; then
    ppc-amigaos-gcc $CFLAGS src/*.c -o build/pdfview $LDFLAGS $LIBS
    ppc-amigaos-strip -o build/pdfview-stripped build/pdfview 2>/dev/null || true
    echo "=== pdfview built ==="
    ls -la build/pdfview build/pdfview-stripped 2>&1 | head -5
else
    echo "=== no src/*.c yet — skipping viewer link ==="
    echo "(add source files under src/ then re-run)"
fi
'
