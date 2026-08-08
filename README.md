# TrapezePDF

A native PDF viewer for AmigaOS 4.1 PPC — built on MuPDF.
By Chris Collins.

## Status

**Alpha** — view/navigate/zoom/print/annotate/form-fill/page-manage
functional. Full v1.0 packaging (LHA, Installer, screenshots) still
in progress. See `docs/ROADMAP.md`.

## Features

- **View** any PDF from AmigaOS shell (`trapezepdf file.pdf`) or via
  File → Open (ASL requester).
- **Multi-page nav** — PgUp/PgDown, arrows, Space, Home/End.
- **Zoom** — Fit Page (default), Fit Width, 100%, or +/- nudge.
- **Print** — File → Print sends the whole document as PostScript
  to `PRT:` (any Prefs-configured printer works).
- **Annotations** — Add sticky notes, delete all annotations on a
  page. Save-as writes them back into the PDF.
- **Form fill** — List form fields on the current page; fill the
  next empty text field. Save-as persists.
- **Page management** — Rotate CW/CCW/180°, delete pages, extract
  a page to a new PDF.
- **Standard OS4 menu** — gadtools menu bar (File/View/Page/Annotate/
  Form/Help).

Not yet supported (post-v1.0):
- Text selection / copy
- Text search
- Thumbnail sidebar
- Interactive mouse annotation (drag-select highlight, freehand draw)

## Building

Prereqs: Docker, `walkero/amigagccondocker:os4-gcc11` (auto-pulled by our
Dockerfile), ~2 GB free disk.

```
docker build -t pdfview-os4-build:local .
./scripts/fetch-mupdf.sh
./scripts/build.sh
```

Output: `build/pdfview-stripped` (~44 MB static ELF32 PPC, big-endian).

## Deploying to guest

Via `xdftool` (guest offline):
```
xdftool ~/AmigaOS4/amigaos4-dev.hdf write \
    build/pdfview-stripped TrapezePDF
```

Via amiga_mcp bridge (guest booted):
```
curl -X POST http://localhost:3000/api/transfer \
    -H 'Content-Type: application/json' \
    -d '{"source":"build/pdfview-stripped","dest":"DH1:TrapezePDF","direction":"push"}'
```

## Running

```
DH1:TrapezePDF                   ; empty window, use File → Open
DH1:TrapezePDF DH1:mydoc.pdf     ; open document at page 1
```

See `docs/USER_GUIDE.md` for full keyboard reference and menu list.

## License

**AGPL-3.0** — matches MuPDF's license. Full text in `LICENSE`.

If you want to embed TrapezePDF (or MuPDF via TrapezePDF) in a
closed-source product, you also need a commercial MuPDF license
from Artifex Software: https://artifex.com/licensing/

## Attribution

- **TrapezePDF** © 2026 Chris Collins
- **MuPDF** © Artifex Software Inc. — https://mupdf.com
- Cross-compile toolchain: walkero's AmigaGCConDocker
