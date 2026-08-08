/*
 * pdfview-os4 — a native PDF viewer for AmigaOS 4.
 * Copyright (C) 2026  pdfview-os4 contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public
 * License along with this program. If not, see
 * <https://www.gnu.org/licenses/>.
 *
 * ---------------------------------------------------------------
 *
 * Phase 1 MVP: opens the PDF given on the command line, renders
 * page 1 at 100% into a fixed 800x600 Intuition window. Any key or
 * close-gadget exits. This proves the mupdf-→-Intuition rendering
 * pipeline works before we build the ReAction UI on top.
 *
 * Argument: full path to a .pdf file (no ASL requester yet).
 * Example:  pdfview  DH1:mydoc.pdf
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <intuition/intuition.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>

#include <mupdf/fitz.h>

/* Fixed window size for Phase 1 — big enough to see something, small
 * enough to fit any reasonable OS4 screen. Later phases will resize
 * to fit page dimensions or open on a chosen screen mode. */
#define WIN_WIDTH   800
#define WIN_HEIGHT  600

static void die(const char *msg) {
    fprintf(stderr, "pdfview: %s\n", msg);
    exit(1);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "usage: pdfview <file.pdf>\n");
        return 1;
    }
    const char *filename = argv[1];

    /* --- MuPDF: init context, open document, load page 1 --- */
    fz_context *ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
    if (!ctx) die("failed to init mupdf context");
    fz_register_document_handlers(ctx);

    fz_document *doc = NULL;
    fz_page *page = NULL;
    fz_pixmap *pix = NULL;

    fz_try(ctx) {
        doc = fz_open_document(ctx, filename);
        if (!doc) fz_throw(ctx, FZ_ERROR_GENERIC, "open_document returned NULL");
        int page_count = fz_count_pages(ctx, doc);
        fprintf(stderr, "pdfview: %s — %d pages\n", filename, page_count);

        page = fz_load_page(ctx, doc, 0);  /* page 1 */
        fz_rect bounds = fz_bound_page(ctx, page);
        fprintf(stderr, "pdfview: page 1 is %.1f x %.1f pt\n",
                bounds.x1 - bounds.x0, bounds.y1 - bounds.y0);

        /* Render at 100% into an RGB pixmap. fz_scale(1,1) = 1:1;
         * later we'll compute a fit-page zoom from the window size
         * vs page dimensions. */
        fz_matrix ctm = fz_scale(1.0f, 1.0f);
        pix = fz_new_pixmap_from_page(ctx, page, ctm, fz_device_rgb(ctx), 0);
        fprintf(stderr, "pdfview: rendered pixmap %dx%d, %d bytes\n",
                pix->w, pix->h, pix->stride * pix->h);
    }
    fz_catch(ctx) {
        fprintf(stderr, "pdfview: mupdf error: %s\n", fz_caught_message(ctx));
        fz_drop_pixmap(ctx, pix);
        fz_drop_page(ctx, page);
        fz_drop_document(ctx, doc);
        fz_drop_context(ctx);
        return 2;
    }

    /* --- Open an Intuition window and blit the pixmap into it --- */
    struct Screen *screen = LockPubScreen(NULL);
    if (!screen) die("LockPubScreen failed");

    struct Window *win = OpenWindowTags(NULL,
        WA_Title,      (uintptr_t)"pdfview",
        WA_Width,      WIN_WIDTH,
        WA_Height,     WIN_HEIGHT,
        WA_DragBar,    TRUE,
        WA_DepthGadget,TRUE,
        WA_CloseGadget,TRUE,
        WA_SizeGadget, FALSE,
        WA_Activate,   TRUE,
        WA_PubScreen,  (uintptr_t)screen,
        WA_IDCMP,      IDCMP_CLOSEWINDOW | IDCMP_RAWKEY,
        TAG_END);
    if (!win) { UnlockPubScreen(NULL, screen); die("OpenWindow failed"); }

    /* MuPDF pixmap is packed RGB (3 bytes per pixel). OS4's
     * WritePixelArray with PIXF_R8G8B8 matches that layout.
     * Clip to window client area (page is likely larger than
     * 800x600 at 100%). Draw at top-left of window client area. */
    int src_w = pix->w, src_h = pix->h;
    int copy_w = src_w < WIN_WIDTH  ? src_w : WIN_WIDTH;
    int copy_h = src_h < WIN_HEIGHT ? src_h : WIN_HEIGHT;
    int dst_x = win->BorderLeft;
    int dst_y = win->BorderTop;

    WritePixelArray(pix->samples,
                    0, 0,                 /* src x, y */
                    pix->stride,          /* src bytes per row */
                    PIXF_R8G8B8,          /* src pixel format */
                    win->RPort,           /* destination RastPort */
                    dst_x, dst_y,
                    copy_w, copy_h);

    /* --- Event loop: wait for close or any key --- */
    BOOL running = TRUE;
    while (running) {
        WaitPort(win->UserPort);
        struct IntuiMessage *msg;
        while ((msg = (struct IntuiMessage*)GetMsg(win->UserPort))) {
            ULONG class = msg->Class;
            ReplyMsg((struct Message*)msg);
            if (class == IDCMP_CLOSEWINDOW || class == IDCMP_RAWKEY)
                running = FALSE;
        }
    }

    /* --- Cleanup --- */
    CloseWindow(win);
    UnlockPubScreen(NULL, screen);
    fz_drop_pixmap(ctx, pix);
    fz_drop_page(ctx, page);
    fz_drop_document(ctx, doc);
    fz_drop_context(ctx);
    return 0;
}
