# pdfview-os4

A native PDF viewer for AmigaOS 4.1 PPC, built on MuPDF.

## Status

**Pre-alpha — not yet functional.** Repo scaffold is in place; source
build not yet green. See `docs/ROADMAP.md` for milestones.

## Goal

A PDF viewer for AmigaOS 4 that is:

- **Native** — Intuition/ReAction UI, no X11/GTK/Qt.
- **Complete** — view, zoom, pan, print, annotate, form fill.
- **Fast** — MuPDF rendering, sensible page cache.
- **Open source** — AGPL-3.0 (matches MuPDF).

## Build target

Cross-compiled from macOS/Linux against the walkero AmigaOS 4 GCC
docker image, same toolchain as `python-amigaos4`. Runs on OS4.1
Final Edition on sam460ex-class hardware (real or QEMU).

## Why another PDF viewer?

APDF and VPDF exist and are perfectly usable for viewing. This
project exists because:

- APDF's xpdf backend is old (xpdf 2.x era). MuPDF's engine is much
  newer and handles modern PDFs (PDF 1.7 features, sensible font
  fallback, encrypted PDFs, XFA form parts) better.
- Neither shipping viewer does form fill or PDF annotations natively.
  Common use case for OS4 users who deal with tax forms, contracts,
  fillable docs.

If your use case is "just open a PDF and look at it", APDF from
OS4Depot works fine today. Try that first.

## Building

### Prerequisites

- Docker with the walkero AmigaOS 4 GCC image (same as
  python-amigaos4). If you don't have it built:
  ```
  cd ../python-amigaos4
  docker build -t amiga-python-build:local .
  ```
- Or use the pdfview-os4 Dockerfile directly:
  ```
  cd pdfview-os4
  docker build -t pdfview-os4-build:local .
  ```

### Build steps

```
./scripts/fetch-mupdf.sh          # clone MuPDF at pinned version
./scripts/build.sh                 # cross-compile everything
```

Outputs:
- `build/pdfview` — the main viewer executable

### Deploy to guest

```
xdftool ~/AmigaOS4/amigaos4-dev.hdf write build/pdfview PDFView
```

## License

AGPL-3.0. See `LICENSE`. MuPDF (the rendering engine) is also
AGPL-3.0; we're built on it.

If you want to embed pdfview-os4 in a closed-source product, you
also need a commercial MuPDF license from Artifex Software
(https://artifex.com/licensing/).

## Contributing

See `CONTRIBUTING.md` (once it exists). For now: file issues on
GitHub, open PRs against `main`. Pre-PR checklist: build must pass
under docker, `./scripts/test.sh` (once it exists) must pass, no
new compiler warnings introduced.

## Related

- [`python-amigaos4`](../python-amigaos4/) — CPython 3.12 for OS4.
  We share the walkero build image and cross-compile approach.
- [`amiga_mcp`](../amiga_mcp/) — the iteration harness used for
  running tests inside a QEMU sam460ex.
