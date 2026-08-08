#!/usr/bin/env bash
# fetch-mupdf.sh — clone MuPDF at a pinned version.
#
# We pin because MuPDF's API and internal file layout change across
# releases and we want reproducible builds.
#
# Usage:  ./scripts/fetch-mupdf.sh
#         (idempotent — skips if mupdf/ already exists at the right tag)
set -euo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"
MUPDF_TAG="${MUPDF_TAG:-1.26.12}"
MUPDF_REPO="${MUPDF_REPO:-https://github.com/ArtifexSoftware/mupdf.git}"
MUPDF_DIR="$HERE/mupdf"

# Rationale for 1.26.12 (as of 2026-08):
# - Latest 1.26.x point release; considered stable branch
# - 1.27 and 1.28 are recent, less field-tested
# - Bump this variable once 1.27/1.28 mature or if we need a specific fix

if [ -d "$MUPDF_DIR/.git" ]; then
    have_tag="$(cd "$MUPDF_DIR" && git describe --tags --exact-match 2>/dev/null || true)"
    if [ "$have_tag" = "$MUPDF_TAG" ]; then
        echo "mupdf/ already at $MUPDF_TAG — nothing to do."
        exit 0
    fi
    echo "mupdf/ present but at '$have_tag'; want '$MUPDF_TAG'."
    echo "Remove mupdf/ and rerun, or set MUPDF_TAG to what you have."
    exit 1
fi

echo "=== cloning MuPDF $MUPDF_TAG ==="
# Shallow clone at the tag + all submodules (freetype, harfbuzz,
# jbig2dec, lcms2, mujs, openjpeg, zlib, jpeg — bundled by MuPDF).
git clone --depth 1 --branch "$MUPDF_TAG" --recurse-submodules \
    --shallow-submodules "$MUPDF_REPO" "$MUPDF_DIR"

# Sanity check
cd "$MUPDF_DIR"
git describe --tags
ls thirdparty/ 2>/dev/null | head -12
echo
echo "MuPDF $MUPDF_TAG ready in $MUPDF_DIR"
