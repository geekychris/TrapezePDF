# pdfview-os4 — User Guide

Current version: **pre-alpha** (2026-08). See `docs/ROADMAP.md` for
what's done and what's not.

## Installation

1. Copy `pdfview` (the binary, ~11 MB stripped) to a drawer of your
   choice, e.g. `SYS:Tools/pdfview`.
2. That's it — no libraries to install, no config files.

## Launching

From an AmigaDOS shell:

```
pdfview                     ; opens with no file loaded, use File→Open
pdfview DH1:mydoc.pdf       ; opens the given PDF at page 1
```

From Workbench: double-click the pdfview icon. If you dropped an
icon on it (e.g. via `WBStartup`-style app-icon integration in a
future release), the PDF file is opened. For now, only argv works.

## Keyboard

| Key(s) | Action |
| ------ | ------ |
| PgDown, Space, → , ↓ | Next page |
| PgUp, Backspace, ← , ↑ | Previous page |
| Home | First page |
| End | Last page |
| F | Fit Page (default) |
| W | Fit Width |
| 1 | 100% zoom |
| + | Zoom in 25% |
| − | Zoom out 20% |
| ESC | Quit |

Menu bar (right-click title bar):

- **File**: Open, Print, Quit
- **View**: First/Last/Prev/Next Page, Fit Page/Fit Width/100%, Zoom In/Out
- **Help**: About

## Printing

`File → Print` renders every page of the current document to a
PostScript envelope, writes it to `T:pdfview_print.ps`, then hands it
to `PRT:` (via `copy T:file PRT:`). Whatever printer driver you have
configured in AmigaOS Prefs handles the actual output.

Requirements:
- A printer configured in Prefs/Printer.
- A PostScript-capable driver, OR TurboPrint (which converts PS to
  many printer languages).

Not yet supported (planned for v0.5):
- Page range / copies dialog (currently always prints all pages).
- Landscape / portrait override.
- Print preview.

## Formats

pdfview reads:
- **PDF** — main format, all PDF 1.7 features except JavaScript
  (disabled at compile time to shrink binary).
- **XPS**, **EPUB**, **HTML** — technically supported by MuPDF but
  our build disables the non-PDF paths to keep the binary compact.

## Known limitations (pre-alpha)

- **No annotations** — Phase 6 not yet implemented.
- **No form fill** — Phase 7 not yet implemented.
- **No page management** — Phase 8 not yet implemented (can't
  rotate/delete/reorder pages).
- **No text selection / copy** — planned post-v1.0.
- **No text search** — planned post-v1.0.
- **No thumbnail sidebar** — planned as part of Phase 8.

## Troubleshooting

**"pdfview: cannot open …"** — either the file doesn't exist at the
given path, or MuPDF couldn't parse it as any known format. Try
opening the file in another viewer to confirm it's valid.

**Blank page** — the page was rendered but its content coordinates
put text outside the visible area. Try `W` (Fit Width) or `1`
(100%) then scroll (scrolling not yet implemented; use zoom to see
different regions).

**Print doesn't come out** — check `Prefs/Printer` is configured
and prints from other apps. `T:pdfview_print.ps` is left temporarily
during printing; you can `copy T:pdfview_print.ps PRT:` yourself to
retry without re-rendering.

**Slow rendering on complex pages** — MuPDF renders on the CPU;
a 300-DPI page of a print-quality PDF can take 5-15 seconds on a
sam460ex. The binary is O2-optimized already; nothing further to
tweak at the user level.

## Reporting bugs

- File on GitHub (once the repo goes public — currently pre-release).
- Include: OS4 version, `pdfview` version, the PDF (or a URL to it),
  and any error messages from the shell.

## License

AGPL-3.0 (matches MuPDF). Source is `~/code/claude_world/pdfview-os4/`
in the development tree. Full LICENSE text ships with the binary
in the LHA archive.
