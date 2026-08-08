# TrapezePDF roadmap

## Milestones

| Phase | Deliverable | Estimated effort |
| ----- | ----------- | ---------------- |
| 0 | Repo scaffold, license, build image, MuPDF fetched and built as static lib | 1–2 days |
| 1 | Skeleton main.c — opens a PDF via argv, renders page 1 to a fixed-size Intuition window, WritePixelArray to RastPort | 2–3 days |
| 2 | Multi-page navigation — PgUp/PgDown/Home/End, page counter status bar, LRU page-render cache | 2 days |
| 3 | ReAction integration — window.class, menu.class, toolbar with page nav + zoom buttons, ASL file requester | 3–5 days |
| 4 | Zoom + pan — fit-page/fit-width/25–400% presets, mouse-drag pan when zoomed | 2–3 days |
| 5 | Printing — File→Print dialog, render pages to bitmap, wrap in PostScript, send to PRT: or via Ghostscript | 3–5 days |
| 6 | Annotations — highlight/underline/note/freehand, save via pdf_annot API, undo/redo | 1–2 weeks |
| 7 | Form fill — pdf_widget detect, ReAction overlay gadgets, save on File→Save | 1 week |
| 8 | Page management — thumbnail sidebar, rotate/delete/reorder/extract | 1 week |
| 9 | v1.0 packaging — docs, screenshots, LHA for OS4Depot, Installer script | 3–5 days |

**Total: ~2–3 months focused work.**

Chosen release model: big-bang. Nothing pushed to origin `main` until
phase 9 lands. Development happens on a `wip/*` branch or local
worktree; `main` gets the polished v1.0.

## Non-goals for v1.0

- **In-place text editing** — MuPDF doesn't support it; PDF isn't
  designed for it. Users who want this need to use LibreOffice Draw
  or similar (not on OS4 natively).
- **PDF creation from scratch** — could compose via `pdf_write_document`
  but no UI for it in v1.0.
- **Signing PDFs with X.509 certificates** — needs AmiSSL integration,
  deferred.
- **Rendering JavaScript-heavy interactive PDFs** — MuPDF's JS support
  is optional; we build without it to keep footprint small.

## Post-v1.0 candidates

- Bookmarks / outline sidebar
- Text search (Ctrl+F)
- Text selection + copy to clipboard.device
- OCR via Tesseract (huge dep — probably not worth it)
- CJK font support (needs bundled CJK fonts — big binary size hit)
- MUI 5 UI variant alongside ReAction
- Colored annotations, callout notes
- Compare-two-PDFs mode
