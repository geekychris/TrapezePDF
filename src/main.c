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
 * Phase 1+2 with basic fit-to-window scaling. Opens the PDF given on
 * the command line, renders each page fit-to-window into an Intuition
 * window, supports PgUp/PgDown/Home/End navigation, ESC or close-gadget
 * to exit. Window resize also re-renders.
 *
 * Argument: full path to a .pdf file.
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
#include <devices/inputevent.h>

#include <mupdf/fitz.h>

/* Initial window dimensions. Later phases add screen-size detection
 * and remember last window bounds via ENV: file. */
#define WIN_WIDTH   800
#define WIN_HEIGHT  600
#define MIN_WIDTH   400
#define MIN_HEIGHT  300

/* --- Raw key codes for OS4 (from intuition/RAWKEY constants) --- */
#define RAWKEY_ESCAPE      0x45
#define RAWKEY_UP          0x4C
#define RAWKEY_DOWN        0x4D
#define RAWKEY_LEFT        0x4F
#define RAWKEY_RIGHT       0x4E
#define RAWKEY_PAGEUP      0x48   /* shift-up */
#define RAWKEY_PAGEDOWN    0x49   /* shift-down */
#define RAWKEY_HOME        0x70
#define RAWKEY_END         0x71
#define RAWKEY_SPACE       0x40   /* also advances page */
#define RAWKEY_BACKSPACE   0x41   /* also goes back */

/* --- Global viewer state (kept small; not thread-shared) --- */
typedef struct {
    fz_context *ctx;
    fz_document *doc;
    int page_count;
    int current_page;      /* 0-based */
    struct Window *win;
    fz_pixmap *pix;        /* current rendered page; NULL until first render */
} viewer_state;

static void die(const char *msg) {
    fprintf(stderr, "pdfview: %s\n", msg);
    exit(1);
}

/* Render current_page fit-to-window into a fresh pixmap. Drops any
 * previous pixmap first. On error leaves state->pix as NULL. */
static void render_current_page(viewer_state *st) {
    if (st->pix) {
        fz_drop_pixmap(st->ctx, st->pix);
        st->pix = NULL;
    }
    if (!st->doc) return;

    fz_page *page = NULL;
    fz_try(st->ctx) {
        page = fz_load_page(st->ctx, st->doc, st->current_page);
        fz_rect bounds = fz_bound_page(st->ctx, page);
        float pw = bounds.x1 - bounds.x0;
        float ph = bounds.y1 - bounds.y0;

        /* Client area inside window borders. */
        int win_w = st->win->Width  - st->win->BorderLeft - st->win->BorderRight;
        int win_h = st->win->Height - st->win->BorderTop  - st->win->BorderBottom;
        if (win_w < 1) win_w = 1;
        if (win_h < 1) win_h = 1;

        /* Fit-page: scale so entire page fits inside client area. */
        float sx = win_w / pw;
        float sy = win_h / ph;
        float scale = sx < sy ? sx : sy;

        fz_matrix ctm = fz_scale(scale, scale);
        st->pix = fz_new_pixmap_from_page(st->ctx, page, ctm,
                                           fz_device_rgb(st->ctx), 0);
    }
    fz_always(st->ctx) {
        fz_drop_page(st->ctx, page);
    }
    fz_catch(st->ctx) {
        fprintf(stderr, "pdfview: render page %d failed: %s\n",
                st->current_page + 1, fz_caught_message(st->ctx));
    }
}

/* Blit current pixmap into window client area, centered if smaller
 * than the client rectangle. Clears background first (gray) so
 * previous pixels don't ghost through when a page is smaller. */
static void redraw(viewer_state *st) {
    struct Window *w = st->win;
    int cx = w->BorderLeft;
    int cy = w->BorderTop;
    int cw = w->Width  - w->BorderLeft - w->BorderRight;
    int ch = w->Height - w->BorderTop  - w->BorderBottom;

    /* Clear client area to a middle gray so partial-page draws don't
     * ghost previous content. */
    SetAPen(w->RPort, 0);
    RectFill(w->RPort, cx, cy, cx + cw - 1, cy + ch - 1);

    if (!st->pix) return;

    int px = st->pix->w, py = st->pix->h;
    /* Center pixmap in client area if smaller. */
    int dx = cx + (cw - px) / 2;
    int dy = cy + (ch - py) / 2;
    if (dx < cx) dx = cx;
    if (dy < cy) dy = cy;

    int copy_w = px < cw ? px : cw;
    int copy_h = py < ch ? py : ch;

    WritePixelArray(st->pix->samples,
                    0, 0, st->pix->stride,
                    PIXF_R8G8B8,
                    w->RPort,
                    dx, dy,
                    copy_w, copy_h);
}

/* Update window title with "pdfview — <filename> — Page X of Y". */
static void update_title(viewer_state *st, const char *filename) {
    static char title[256];
    /* Take basename of filename for the title so long paths don't
     * overflow the title bar. */
    const char *base = strrchr(filename, '/');
    if (!base) base = strrchr(filename, ':');
    if (base) base++; else base = filename;
    snprintf(title, sizeof(title), "pdfview - %s - Page %d of %d",
             base, st->current_page + 1, st->page_count);
    SetWindowTitles(st->win, (STRPTR)title, (STRPTR)-1);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "usage: pdfview <file.pdf>\n");
        return 1;
    }
    const char *filename = argv[1];

    viewer_state st = {0};
    st.ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
    if (!st.ctx) die("failed to init mupdf context");
    fz_register_document_handlers(st.ctx);

    fz_try(st.ctx) {
        st.doc = fz_open_document(st.ctx, filename);
        if (!st.doc) fz_throw(st.ctx, FZ_ERROR_GENERIC, "open_document NULL");
        st.page_count = fz_count_pages(st.ctx, st.doc);
        fprintf(stderr, "pdfview: %s — %d pages\n", filename, st.page_count);
    }
    fz_catch(st.ctx) {
        fprintf(stderr, "pdfview: cannot open %s: %s\n",
                filename, fz_caught_message(st.ctx));
        fz_drop_document(st.ctx, st.doc);
        fz_drop_context(st.ctx);
        return 2;
    }

    struct Screen *screen = LockPubScreen(NULL);
    if (!screen) die("LockPubScreen failed");

    st.win = OpenWindowTags(NULL,
        WA_Title,         (uintptr_t)"pdfview",
        WA_Width,         WIN_WIDTH,
        WA_Height,        WIN_HEIGHT,
        WA_MinWidth,      MIN_WIDTH,
        WA_MinHeight,     MIN_HEIGHT,
        WA_MaxWidth,      screen->Width,
        WA_MaxHeight,     screen->Height,
        WA_DragBar,       TRUE,
        WA_DepthGadget,   TRUE,
        WA_CloseGadget,   TRUE,
        WA_SizeGadget,    TRUE,
        WA_SizeBRight,    TRUE,
        WA_SizeBBottom,   TRUE,
        WA_Activate,      TRUE,
        WA_PubScreen,     (uintptr_t)screen,
        WA_IDCMP,         IDCMP_CLOSEWINDOW | IDCMP_RAWKEY |
                          IDCMP_NEWSIZE | IDCMP_REFRESHWINDOW,
        TAG_END);
    if (!st.win) {
        UnlockPubScreen(NULL, screen);
        fz_drop_document(st.ctx, st.doc);
        fz_drop_context(st.ctx);
        die("OpenWindow failed");
    }

    /* Initial render + title. */
    render_current_page(&st);
    update_title(&st, filename);
    redraw(&st);

    /* Event loop. */
    BOOL running = TRUE;
    while (running) {
        WaitPort(st.win->UserPort);
        struct IntuiMessage *msg;
        while ((msg = (struct IntuiMessage*)GetMsg(st.win->UserPort))) {
            ULONG class = msg->Class;
            UWORD code  = msg->Code;
            BOOL is_key_press = !(code & IECODE_UP_PREFIX);
            UWORD keycode = code & ~IECODE_UP_PREFIX;
            ReplyMsg((struct Message*)msg);

            if (class == IDCMP_CLOSEWINDOW) {
                running = FALSE;
                break;
            }
            if (class == IDCMP_NEWSIZE) {
                render_current_page(&st);
                redraw(&st);
                continue;
            }
            if (class == IDCMP_REFRESHWINDOW) {
                BeginRefresh(st.win);
                redraw(&st);
                EndRefresh(st.win, TRUE);
                continue;
            }
            if (class == IDCMP_RAWKEY && is_key_press) {
                int new_page = st.current_page;
                switch (keycode) {
                case RAWKEY_ESCAPE:
                    running = FALSE;
                    break;
                case RAWKEY_PAGEDOWN:
                case RAWKEY_SPACE:
                case RAWKEY_RIGHT:
                case RAWKEY_DOWN:
                    if (new_page < st.page_count - 1) new_page++;
                    break;
                case RAWKEY_PAGEUP:
                case RAWKEY_BACKSPACE:
                case RAWKEY_LEFT:
                case RAWKEY_UP:
                    if (new_page > 0) new_page--;
                    break;
                case RAWKEY_HOME:
                    new_page = 0;
                    break;
                case RAWKEY_END:
                    new_page = st.page_count - 1;
                    break;
                default:
                    break;
                }
                if (new_page != st.current_page) {
                    st.current_page = new_page;
                    render_current_page(&st);
                    update_title(&st, filename);
                    redraw(&st);
                }
            }
        }
    }

    CloseWindow(st.win);
    UnlockPubScreen(NULL, screen);
    if (st.pix) fz_drop_pixmap(st.ctx, st.pix);
    fz_drop_document(st.ctx, st.doc);
    fz_drop_context(st.ctx);
    return 0;
}
