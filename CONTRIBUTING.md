# Contributing to pdfview-os4

## Development environment

You need:
- A UNIX-like host (macOS or Linux) with Docker.
- The walkero OS4 GCC docker image (auto-pulled via our Dockerfile).
- ~2 GB free disk for MuPDF source + build artifacts.

Bootstrap:
```
git clone <this repo>
cd pdfview-os4
docker build -t pdfview-os4-build:local .
./scripts/fetch-mupdf.sh
./scripts/build.sh
```

Output: `build/pdfview` (unstripped) + `build/pdfview-stripped`.

Iteration cycle:
```
# edit src/main.c (or wherever)
./scripts/build.sh              # ~30s incremental, ~15min from scratch

# Deploy to QEMU (guest running):
curl -X POST http://localhost:3000/api/transfer -H 'Content-Type: application/json' \
    -d '{"source":"build/pdfview-stripped","dest":"DH1:pdfview","direction":"push"}'
# Or if guest is stopped:
xdftool ~/AmigaOS4/amigaos4-dev.hdf write build/pdfview-stripped pdfview

# Run:
curl -X POST http://localhost:3000/api/launch -H 'Content-Type: application/json' \
    -d '{"command":"DH1:pdfview DH1:test.pdf"}'
```

## Style

- **C99, plain C** — no C++, no exotic extensions. Compiles cleanly
  under GCC 11 with `-Wall`.
- **Amiga naming** — camelCase for OS4 API (matches SDK convention),
  snake_case for our own functions.
- **License headers** — every source file must have AGPL-3.0 header
  (copy from `src/main.c`).
- **Line width** — 78 characters preferred.
- **Comments** — explain non-obvious *why*, not *what*.

## Pre-PR checklist

Before opening a PR:

1. **Build clean** — `./scripts/build.sh` succeeds with no new
   warnings.
2. **Boot test** — deploy to QEMU sam460ex guest, `pdfview DH1:test.pdf`
   opens a window without a crash.
3. **Screenshot** — for UI changes, attach a screenshot of the guest
   window showing the change working.
4. **No new lib deps** — adding third-party libraries requires
   discussion first (MuPDF ships 8 already, we don't want more).

## Phase order (roadmap)

Contributing effort is best spent in the phase order documented in
`docs/ROADMAP.md`. Skipping ahead — e.g. writing annotation UI
before menus exist — is fine if you scope it as an experiment, but
merge order should still respect the roadmap.

## Code review

Small PRs preferred (< 500 lines diff). One phase = one PR generally.
Reviewer should verify:
- Compiles clean
- Runs on guest without crashing
- Doesn't break previous-phase features
- Follows style
- Includes doc updates if user-facing behavior changed

## Where things live

```
src/main.c              main viewer — event loop, render/redraw,
                        menu handlers, print pipeline
src/os4_shims.c         POSIX/BSD functions newlib on OS4 lacks
                        (timegm etc.)
src/compat/memory.h     legacy header shim for third-party libs
scripts/fetch-mupdf.sh  pins MuPDF version + submodule init
scripts/build.sh        docker-based cross-compile pipeline
mupdf/                  submodule at pinned tag (v1.26.12)
docs/                   roadmap, user guide, this file
```

## MuPDF version bumps

Change `MUPDF_TAG` in `scripts/fetch-mupdf.sh`. Rerun `./scripts/fetch-mupdf.sh`
after removing `mupdf/`. Rebuild libmupdf clean:
```
rm -rf mupdf/build
./scripts/build.sh mupdf
```

Watch for API changes in MuPDF's `include/mupdf/fitz.h` and
`include/mupdf/pdf.h` — bumps across minor versions occasionally
change function signatures (esp. around `fz_matrix` and error
handling).

## Testing

There's no automated test suite yet. Manual test protocol:
1. Open a known-good PDF (attach a canonical test PDF once we
   pick one — plans to include a `tests/fixtures/` dir).
2. Verify first page renders correctly.
3. Press PgDown, verify page 2 appears.
4. Press Home, verify back at page 1.
5. Menu → File → Open — verify ASL requester appears.
6. Menu → File → Print — verify T:pdfview_print.ps is created
   and content is valid PostScript.

## License headers

Every new source file starts with:
```c
/*
 * pdfview-os4 — a native PDF viewer for AmigaOS 4.
 * Copyright (C) 2026  pdfview-os4 contributors
 *
 * Licensed under the GNU Affero General Public License v3 or later.
 * See LICENSE for full terms.
 */
```
