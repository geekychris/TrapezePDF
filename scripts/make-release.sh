#!/usr/bin/env bash
# make-release.sh — build the TrapezePDF LHA archive for OS4Depot.
#
# Produces:
#   build/TrapezePDF-<version>.lha
#
# Layout inside the LHA (matches OS4Depot's Application/Office standard):
#   TrapezePDF/           top-level drawer
#     TrapezePDF          binary
#     TrapezePDF.info     Workbench icon (placeholder .info if not present)
#     Install             Installer script (AmigaOS 4 Installer.LHA compatible)
#     Install.info        Installer icon (placeholder)
#     README.md
#     LICENSE
#     docs/
#       USER_GUIDE.md
#       ROADMAP.md
#
# Usage:  ./scripts/make-release.sh [version]
#         default version: "0.1-alpha"
set -euo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="${1:-0.1-alpha}"
STAGING="$HERE/build/release/TrapezePDF"
LHA_OUT="$HERE/build/TrapezePDF-$VERSION.lha"

echo "=== staging release directory ==="
rm -rf "$HERE/build/release"
mkdir -p "$STAGING/docs"

# Binary — strip if we have the stripped version
if [ -f "$HERE/build/pdfview-stripped" ]; then
    cp "$HERE/build/pdfview-stripped" "$STAGING/TrapezePDF"
elif [ -f "$HERE/build/pdfview" ]; then
    cp "$HERE/build/pdfview" "$STAGING/TrapezePDF"
else
    echo "ERROR: no binary at build/pdfview[-stripped]. Run ./scripts/build.sh first." >&2
    exit 1
fi

# Docs + license
cp "$HERE/README.md"           "$STAGING/README.md"
cp "$HERE/LICENSE"             "$STAGING/LICENSE"
cp "$HERE/CONTRIBUTING.md"     "$STAGING/docs/CONTRIBUTING.md"
cp "$HERE/docs/USER_GUIDE.md"  "$STAGING/docs/USER_GUIDE.md"
cp "$HERE/docs/ROADMAP.md"     "$STAGING/docs/ROADMAP.md"

# Installer script (see below — generated from template)
cat > "$STAGING/Install" <<'EOF'
; TrapezePDF Installer — AmigaOS 4 Installer.LHA compatible
;
; Copyright (C) 2026 Chris Collins.  AGPL-3.0.

(welcome
    "This Installer will install TrapezePDF - a native PDF viewer\n"
    "for AmigaOS 4.\n\n"
    "TrapezePDF by Chris Collins, built on MuPDF by Artifex.\n"
    "Licensed under the GNU Affero General Public License v3.")

(set @default-dest "SYS:Utilities")

(set target
    (askdir
        (prompt "Where should TrapezePDF be installed?\n"
                "A drawer named 'TrapezePDF' will be created.")
        (help @askdir-help)
        (default @default-dest)))

(set full-target (tackon target "TrapezePDF"))

(makedir full-target
    (prompt "Creating drawer " full-target)
    (help @makedir-help)
    (confirm))

(copyfiles
    (prompt "Copying TrapezePDF binary...")
    (help @copyfiles-help)
    (source "TrapezePDF")
    (dest full-target))

(copyfiles
    (prompt "Copying README, LICENSE, and docs...")
    (help @copyfiles-help)
    (source "")
    (dest full-target)
    (choices "README.md" "LICENSE" "docs"))

(protect (tackon full-target "TrapezePDF") "+rwe")

(message
    "TrapezePDF installed to " full-target ".\n\n"
    "To use:\n"
    "  " full-target "/TrapezePDF DH1:mydoc.pdf\n"
    "or:\n"
    "  " full-target "/TrapezePDF https://example.com/foo.pdf\n\n"
    "For HTTPS URLs you need `openssl` binary at DH1:openssl\n"
    "(from AmiSSL 5.27+), and AmiSSL certs at DH1:AmiSSL/Certs.")

(exit)
EOF

# Placeholder icon files — these ship as zero-byte in a v0.1-alpha
# release; anyone with IconEdit can attach real icons later. AmigaOS
# treats missing .info files as "no icon", not an error.
touch "$STAGING/TrapezePDF.info"
touch "$STAGING/Install.info"

echo "=== staging tree ==="
find "$STAGING" -print

echo
echo "=== building LHA archive ==="
cd "$HERE/build/release"
rm -f "$LHA_OUT"
# LHA creation isn't available on macOS (Lhasa is read-only). Ship
# a .tar.gz + .zip; convert to LHA on an OS4 guest or Linux host with
# `jlha-utils` installed before submitting to OS4Depot.
TARBALL="$HERE/build/TrapezePDF-$VERSION.tar.gz"
ZIPFILE="$HERE/build/TrapezePDF-$VERSION.zip"
tar -czf "$TARBALL" TrapezePDF
zip -qr  "$ZIPFILE" TrapezePDF
LHA_OUT="$TARBALL"   # for the "ready" message below
# Try LHA creation if a real lha binary is present (typically on
# Linux; brew's `lhasa` is read-only).
if lha 2>&1 | grep -q "add"; then
    lha -a "$HERE/build/TrapezePDF-$VERSION.lha" TrapezePDF && \
        LHA_OUT="$HERE/build/TrapezePDF-$VERSION.lha"
fi

echo
echo "=== release archives ==="
ls -la "$HERE/build/TrapezePDF-$VERSION"* 2>&1
echo
echo "SHA256:"
shasum -a 256 "$HERE/build/TrapezePDF-$VERSION"* 2>&1
echo
echo "Release archives ready. Upload to OS4Depot:"
echo "  https://os4depot.net/index.php?function=upload"
echo "Note: OS4Depot prefers .lha; convert on an OS4 guest with:"
echo "  lha a TrapezePDF-$VERSION.lha TrapezePDF"
echo "or on Linux with jlha-utils."
