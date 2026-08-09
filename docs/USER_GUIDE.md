# TrapezePDF — User Guide

Current status: **alpha** (2026-08). All Phase 1–8 features are
implemented (view/navigate/zoom/print/annotate/form-fill/page-manage).
See `docs/ROADMAP.md` for what's still on the post-v1.0 list.

## Installation

1. Copy `TrapezePDF` (~44 MB stripped) to a drawer of your choice,
   e.g. `SYS:Tools/TrapezePDF` or `DH1:TrapezePDF`.
2. For HTTPS URL support only: place the AmiSSL 5.27+ `openssl`
   binary at `DH1:openssl` and the cert bundle at `DH1:AmiSSL/Certs/`.
3. Nothing else — MuPDF and all image/font decoders are statically
   linked. No `LIBS:` install, no configuration files.

## Launching

From an AmigaDOS shell:

```
DH1:TrapezePDF                                ; empty window; use File → Open
DH1:TrapezePDF DH1:mydoc.pdf                  ; local file
DH1:TrapezePDF https://host/path/file.pdf     ; HTTPS fetch + open
DH1:TrapezePDF http://host/path/file.pdf      ; not implemented yet — use https
```

From Workbench: double-click the `TrapezePDF` icon (v1.0 will add
tool-icon drop-onto support; for now argv-only).

### HTTPS URL fetch — what happens

1. TrapezePDF parses the URL, splits scheme/host/port/path.
2. Writes an HTTP/1.0 `GET` request to `T:trapeze_req`.
3. Runs `type T:trapeze_req | DH1:openssl s_client -connect
   host:port -servername host -quiet -CApath DH1:AmiSSL/Certs
   >T:trapeze_raw`.
4. Parses the HTTP response headers, honours `Content-Length` to
   bound the body precisely (so trailing openssl session-info text
   doesn't leak past `%%EOF`), and saves the body to
   `T:trapeze_download.pdf`.
5. Opens that file in the viewer.
6. Any failure at any step is logged to `T:trapeze_fetch.log` —
   check it if a URL launch shows an empty or crashy window.

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

Menu shortcuts use Right-Amiga plus the letter shown next to the
menu item.

## Menu bar

### File
| Item | Shortcut | Behaviour |
| ---- | -------- | --------- |
| Open… | R-A + O | ASL file requester → open PDF |
| Open URL from Clipboard | R-A + U | Read clipboard.device (IFF FTXT/CHRS), parse as URL, fetch via HTTPS |
| Save As… | R-A + S | ASL save requester → `pdf_save_document`; persists annotations + form fills + page rotations/deletions |
| Print… | R-A + P | Render every page to PostScript at `T:pdfview_print.ps`, then `copy … PRT:` |
| Print to File… | (none) | ASL save requester (defaults to `<pdfname>.ps`) → write same PostScript to that path |
| Quit | R-A + Q | Exit |

### View
Zoom + navigation duplicates of the keyboard shortcuts above.

### Page
| Item | Shortcut | Behaviour |
| ---- | -------- | --------- |
| Rotate 90 CW | R-A + R | Rotate current page 90° clockwise |
| Rotate 90 CCW | R-A + L | Rotate current page 90° counter-clockwise |
| Rotate 180 | (none) | Flip current page |
| Delete Page | (none) | Remove current page from the in-memory PDF (save-as to persist) |
| Extract Page | (none) | ASL save requester → write current page as a standalone PDF |

### Annotate
| Item | Shortcut | Behaviour |
| ---- | -------- | --------- |
| Add Sticky Note… | R-A + N | Prompt for the note text in a small dialog, then place a sticky-note icon at the centre of the current page |
| Delete All on Page | (none) | Remove every annotation on the current page |

Annotations persist only after `File → Save As…`.

**About sticky notes:** per the PDF spec, a sticky note is a
fixed-size icon marker (typically 24×24 points). Long content lives
in the annotation's `/Contents` field, and readers that support text
annotations (Adobe Reader, Foxit, MuPDF's `mutool`) show that text
in a pop-up when the icon is clicked. TrapezePDF places the icon at
page centre; it is NOT a resizable text box. If you want an on-page
text block, that's a "Free Text" annotation (`PDF_ANNOT_FREE_TEXT`)
which requires interactive drag-select and isn't in this release
(planned post-v1.0 alongside highlight-with-mouse and freehand).

### Form
| Item | Shortcut | Behaviour |
| ---- | -------- | --------- |
| List Form Fields | (none) | Print every `pdf_widget` on the page to stderr — name, type, value |
| Fill Next Field… | (none) | Find the next empty text field on the page, prompt for its value |

Form fills persist only after `File → Save As…`.

### Help
| About | Product/copyright/license/MuPDF version dialog. |

## Printing

### To a printer (File → Print)

Renders every page to PostScript at `T:pdfview_print.ps`, then
`copy T:pdfview_print.ps PRT:`. Whichever printer driver is
configured in Prefs/Printer handles the actual output.

Requirements:
- A printer configured in Prefs/Printer.
- A PostScript-capable driver, OR TurboPrint (which converts PS to
  many printer languages).

### To a file (File → Print to File…)

Same PostScript output, written directly to a user-chosen `.ps`
path. Useful for:

- **Proofing** — check what would be sent to the printer without
  spinning one up.
- **Ghostscript conversion** — carry the `.ps` to another machine
  and run `ps2pdf`, `gs -sDEVICE=png16m`, etc.
- **Debugging** — see exactly what the print path emits when a
  physical print looks wrong.

Not yet supported (planned for v0.5):
- Page range / copies dialog (currently always prints all pages).
- Landscape / portrait override.
- Print preview.

## Formats

TrapezePDF reads:
- **PDF** — all PDF 1.7 features except JavaScript (disabled at
  compile time to shrink binary and reduce attack surface).
- **XPS / EPUB / HTML** — technically supported by MuPDF but our
  build disables the non-PDF paths to keep the binary compact.

## Troubleshooting

**"TrapezePDF: cannot open …"** — the file doesn't exist at the
given path, or MuPDF couldn't parse it as any known format. Try
opening it in another viewer to confirm it's valid.

**URL launch shows a blank window** — check `T:trapeze_fetch.log`
for the exact failure. Common causes:
- No default route on the guest → "No route to host" from openssl.
  Fix: `C:AddNetRoute DEFAULTGATEWAY=<your_gateway>` at boot.
- Wrong `Host:` header (fixed in the current build; older builds
  wrote `Host: hostname0` for standard ports and got "Site not
  found" from shared-IP hosting like GitHub Pages).
- Trailing openssl session-info text past `%%EOF` (fixed via
  Content-Length parsing).

**Blank page but window opens** — the page rendered but its
content coordinates put text outside the visible area. Try `W`
(Fit Width) or `1` (100%).

**Print doesn't come out** — check `Prefs/Printer` is configured
and prints from other apps. `T:pdfview_print.ps` is left temporarily
during printing; you can `copy T:pdfview_print.ps PRT:` yourself to
retry without re-rendering. Or use Print to File to inspect the PS.

**Slow rendering on complex pages** — MuPDF renders on the CPU;
a 300-DPI page of a print-quality PDF can take 5–15 seconds on a
sam460ex. The binary is `-O2` already; nothing further to tweak
at the user level.

## Reporting bugs

- File on GitHub: https://github.com/geekychris/TrapezePDF
- Include: OS4 version, TrapezePDF version, the PDF (or a URL to
  it), and any error messages from the shell.
- For URL-fetch bugs, attach `T:trapeze_fetch.log`.

## License

AGPL-3.0 — matches MuPDF's license. Full text in `LICENSE`.
