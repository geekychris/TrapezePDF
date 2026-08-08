/*
 * pdfview-os4 — a native PDF viewer for AmigaOS 4.
 * Copyright (C) 2026  pdfview-os4 contributors
 *
 * Licensed under the GNU Affero General Public License v3 or later.
 * See LICENSE for full terms.
 *
 * ---------------------------------------------------------------
 *
 * Phases 1–5: view + navigation + fit-page/zoom + printing +
 * standard-menu UI with ASL file requester for File→Open.
 *
 * Keyboard:
 *   PgUp/PgDown, Space/Backspace, arrow keys — prev/next page
 *   Home/End                                  — first/last page
 *   +/-                                       — zoom in/out (10%)
 *   F                                          — fit-page (default)
 *   W                                          — fit-width
 *   1                                          — 100%
 *   ESC or close-gadget                        — exit
 *
 * Menu: File → Open, Print, Quit
 *       View → First Page, Last Page, Prev, Next, Fit Page, Fit Width, 100%
 *       Help → About
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/asl.h>
#include <proto/gadtools.h>
#include <intuition/intuition.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <devices/inputevent.h>
#include <libraries/asl.h>
#include <libraries/gadtools.h>

#include <mupdf/fitz.h>

#define WIN_WIDTH   800
#define WIN_HEIGHT  600
#define MIN_WIDTH   400
#define MIN_HEIGHT  300

/* Raw key codes for OS4 (Amiga RAWKEY constants) */
#define RAWKEY_ESCAPE      0x45
#define RAWKEY_UP          0x4C
#define RAWKEY_DOWN        0x4D
#define RAWKEY_LEFT        0x4F
#define RAWKEY_RIGHT       0x4E
#define RAWKEY_PAGEUP      0x48
#define RAWKEY_PAGEDOWN    0x49
#define RAWKEY_HOME        0x70
#define RAWKEY_END         0x71
#define RAWKEY_SPACE       0x40
#define RAWKEY_BACKSPACE   0x41
#define RAWKEY_F1          0x50
#define RAWKEY_PLUS        0x5B    /* numpad +, may vary */
#define RAWKEY_MINUS       0x4A    /* numpad - */

/* Zoom modes */
typedef enum {
    ZOOM_FIT_PAGE,
    ZOOM_FIT_WIDTH,
    ZOOM_CUSTOM       /* zoom_factor holds the value */
} zoom_mode_t;

/* Menu item IDs (arbitrary; used as UserData for menu items) */
enum {
    MNU_FILE_OPEN = 1,
    MNU_FILE_PRINT,
    MNU_FILE_QUIT,
    MNU_VIEW_FIRST,
    MNU_VIEW_LAST,
    MNU_VIEW_PREV,
    MNU_VIEW_NEXT,
    MNU_VIEW_FIT_PAGE,
    MNU_VIEW_FIT_WIDTH,
    MNU_VIEW_100,
    MNU_VIEW_ZOOM_IN,
    MNU_VIEW_ZOOM_OUT,
    MNU_HELP_ABOUT
};

typedef struct {
    fz_context   *ctx;
    fz_document  *doc;
    char          filepath[512];   /* full path of currently-open doc */
    int           page_count;
    int           current_page;    /* 0-based */
    zoom_mode_t   zoom_mode;
    float         zoom_factor;     /* only used when zoom_mode==CUSTOM */
    struct Window *win;
    fz_pixmap    *pix;
} viewer_state;

static void die(const char *msg) {
    fprintf(stderr, "pdfview: %s\n", msg);
    exit(1);
}

/* --- Menu construction ------------------------------------------------
 * NewMenu-array approach: simplest way to build a full menu strip on OS4.
 * Uses NM_TITLE for menu, NM_ITEM for entries, NM_END to close. */
static struct NewMenu menu_data[] = {
    { NM_TITLE, "File",         0, 0, 0, 0                        },
    { NM_ITEM,  "Open...",      "O", 0, 0, (APTR)MNU_FILE_OPEN     },
    { NM_ITEM,  "Print...",     "P", 0, 0, (APTR)MNU_FILE_PRINT    },
    { NM_ITEM,  NM_BARLABEL,    0, 0, 0, 0                        },
    { NM_ITEM,  "Quit",         "Q", 0, 0, (APTR)MNU_FILE_QUIT     },

    { NM_TITLE, "View",         0, 0, 0, 0                        },
    { NM_ITEM,  "First Page",   0,   0, 0, (APTR)MNU_VIEW_FIRST    },
    { NM_ITEM,  "Previous",     0,   0, 0, (APTR)MNU_VIEW_PREV     },
    { NM_ITEM,  "Next",         0,   0, 0, (APTR)MNU_VIEW_NEXT     },
    { NM_ITEM,  "Last Page",    0,   0, 0, (APTR)MNU_VIEW_LAST     },
    { NM_ITEM,  NM_BARLABEL,    0,   0, 0, 0                       },
    { NM_ITEM,  "Fit Page",     "F", 0, 0, (APTR)MNU_VIEW_FIT_PAGE  },
    { NM_ITEM,  "Fit Width",    "W", 0, 0, (APTR)MNU_VIEW_FIT_WIDTH },
    { NM_ITEM,  "100%",         "1", 0, 0, (APTR)MNU_VIEW_100      },
    { NM_ITEM,  "Zoom In",      "+", 0, 0, (APTR)MNU_VIEW_ZOOM_IN  },
    { NM_ITEM,  "Zoom Out",     "-", 0, 0, (APTR)MNU_VIEW_ZOOM_OUT },

    { NM_TITLE, "Help",         0, 0, 0, 0                        },
    { NM_ITEM,  "About...",     "?", 0, 0, (APTR)MNU_HELP_ABOUT    },

    { NM_END,   0,              0, 0, 0, 0                        }
};

/* Compute the effective scale factor given the zoom mode + window dims. */
static float compute_scale(viewer_state *st, float page_w, float page_h,
                            int win_w, int win_h)
{
    switch (st->zoom_mode) {
    case ZOOM_FIT_PAGE: {
        float sx = win_w / page_w;
        float sy = win_h / page_h;
        return sx < sy ? sx : sy;
    }
    case ZOOM_FIT_WIDTH:
        return win_w / page_w;
    case ZOOM_CUSTOM:
    default:
        return st->zoom_factor;
    }
}

static void render_current_page(viewer_state *st)
{
    if (st->pix) { fz_drop_pixmap(st->ctx, st->pix); st->pix = NULL; }
    if (!st->doc) return;

    fz_page *page = NULL;
    fz_try(st->ctx) {
        page = fz_load_page(st->ctx, st->doc, st->current_page);
        fz_rect bounds = fz_bound_page(st->ctx, page);
        float pw = bounds.x1 - bounds.x0;
        float ph = bounds.y1 - bounds.y0;

        int win_w = st->win->Width  - st->win->BorderLeft - st->win->BorderRight;
        int win_h = st->win->Height - st->win->BorderTop  - st->win->BorderBottom;
        if (win_w < 1) win_w = 1;
        if (win_h < 1) win_h = 1;

        float scale = compute_scale(st, pw, ph, win_w, win_h);
        fz_matrix ctm = fz_scale(scale, scale);
        st->pix = fz_new_pixmap_from_page(st->ctx, page, ctm,
                                            fz_device_rgb(st->ctx), 0);
    }
    fz_always(st->ctx) { fz_drop_page(st->ctx, page); }
    fz_catch(st->ctx) {
        fprintf(stderr, "pdfview: render page %d failed: %s\n",
                st->current_page + 1, fz_caught_message(st->ctx));
    }
}

static void redraw(viewer_state *st)
{
    struct Window *w = st->win;
    int cx = w->BorderLeft;
    int cy = w->BorderTop;
    int cw = w->Width  - w->BorderLeft - w->BorderRight;
    int ch = w->Height - w->BorderTop  - w->BorderBottom;

    SetAPen(w->RPort, 0);
    RectFill(w->RPort, cx, cy, cx + cw - 1, cy + ch - 1);

    if (!st->pix) return;

    int px = st->pix->w, py = st->pix->h;
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

static const char *basename_of(const char *path)
{
    const char *b = strrchr(path, '/');
    if (!b) b = strrchr(path, ':');
    return b ? b + 1 : path;
}

static void update_title(viewer_state *st)
{
    static char title[256];
    const char *base = basename_of(st->filepath);
    const char *zoom_str = "Fit";
    char zoom_buf[16];
    if (st->zoom_mode == ZOOM_FIT_WIDTH) zoom_str = "Width";
    else if (st->zoom_mode == ZOOM_CUSTOM) {
        snprintf(zoom_buf, sizeof(zoom_buf), "%.0f%%",
                 st->zoom_factor * 100.0f);
        zoom_str = zoom_buf;
    }
    snprintf(title, sizeof(title),
             "pdfview - %s - Page %d/%d [%s]",
             base, st->current_page + 1, st->page_count, zoom_str);
    SetWindowTitles(st->win, (STRPTR)title, (STRPTR)-1);
}

/* Load a new document, replacing the current one. */
static void open_document(viewer_state *st, const char *path)
{
    fz_document *newdoc = NULL;
    fz_try(st->ctx) {
        newdoc = fz_open_document(st->ctx, path);
        if (!newdoc) fz_throw(st->ctx, FZ_ERROR_GENERIC, "open_document NULL");
    }
    fz_catch(st->ctx) {
        fprintf(stderr, "pdfview: cannot open %s: %s\n",
                path, fz_caught_message(st->ctx));
        return;
    }
    if (st->doc) fz_drop_document(st->ctx, st->doc);
    st->doc = newdoc;
    st->page_count = fz_count_pages(st->ctx, newdoc);
    st->current_page = 0;
    strncpy(st->filepath, path, sizeof(st->filepath) - 1);
    st->filepath[sizeof(st->filepath) - 1] = '\0';
    render_current_page(st);
    update_title(st);
    redraw(st);
}

/* File→Open — ASL requester (asl.library). */
static void action_file_open(viewer_state *st)
{
    struct FileRequester *req = AllocAslRequestTags(ASL_FileRequest,
        ASLFR_TitleText,  (uintptr_t)"Open PDF",
        ASLFR_DoPatterns, TRUE,
        ASLFR_InitialPattern, (uintptr_t)"#?.pdf",
        ASLFR_Window,     (uintptr_t)st->win,
        TAG_END);
    if (!req) return;
    if (AslRequestTags(req, TAG_END)) {
        char path[512];
        if (req->fr_Drawer && req->fr_Drawer[0]) {
            snprintf(path, sizeof(path), "%s/%s",
                     req->fr_Drawer, req->fr_File);
            /* AmigaDOS: drawer might end with ':' — no slash needed */
            size_t dl = strlen(req->fr_Drawer);
            if (dl > 0 && req->fr_Drawer[dl - 1] == ':')
                snprintf(path, sizeof(path), "%s%s",
                         req->fr_Drawer, req->fr_File);
        } else {
            snprintf(path, sizeof(path), "%s", req->fr_File);
        }
        open_document(st, path);
    }
    FreeAslRequest(req);
}

/* File→Print — render each page to PGM (grayscale PBM) then send
 * via `type` to PRT: which routes through OS4 printer prefs. Simple
 * pipeline: no dialog for page range/copies in this pass, prints all
 * pages. Uses Ghostscript-independent path (renders through MuPDF,
 * writes bitmap directly to PRT:). */
static void action_file_print(viewer_state *st)
{
    if (!st->doc) return;
    fprintf(stderr, "pdfview: print — rendering %d pages via mupdf...\n",
            st->page_count);
    /* Write a PostScript envelope to T:pdfview_print.ps then send to PRT:
     * via `copy T:pdfview_print.ps PRT:` — OS4 printer.device handles the
     * driver. Delegate the actual PS generation to `mutool convert`
     * fallback if available; otherwise render to PS via MuPDF's built-in
     * `pdfwrite`-equivalent path. */

    char ps_path[] = "T:pdfview_print.ps";
    fz_document_writer *wri = NULL;
    fz_try(st->ctx) {
        wri = fz_new_document_writer(st->ctx, ps_path, "ps", NULL);
        for (int i = 0; i < st->page_count; i++) {
            fz_page *page = fz_load_page(st->ctx, st->doc, i);
            fz_rect r = fz_bound_page(st->ctx, page);
            fz_device *dev = fz_begin_page(st->ctx, wri, r);
            fz_run_page(st->ctx, page, dev, fz_identity, NULL);
            fz_end_page(st->ctx, wri);
            fz_drop_page(st->ctx, page);
        }
        fz_close_document_writer(st->ctx, wri);
    }
    fz_always(st->ctx) { fz_drop_document_writer(st->ctx, wri); }
    fz_catch(st->ctx) {
        fprintf(stderr, "pdfview: print — PS writer failed: %s\n",
                fz_caught_message(st->ctx));
        return;
    }
    /* Now shell out via os.system-equivalent — DOS `Execute` on OS4. */
    system("copy T:pdfview_print.ps PRT: QUIET");
    system("delete T:pdfview_print.ps QUIET");
    fprintf(stderr, "pdfview: print — sent to PRT:\n");
}

/* Handle a menu selection. Returns FALSE if the user chose Quit. */
static BOOL handle_menu(viewer_state *st, UWORD menu_num)
{
    struct MenuItem *item = ItemAddress(st->win->MenuStrip, menu_num);
    if (!item) return TRUE;
    ULONG id = (ULONG)(uintptr_t)GTMENUITEM_USERDATA(item);

    switch (id) {
    case MNU_FILE_OPEN:  action_file_open(st); break;
    case MNU_FILE_PRINT: action_file_print(st); break;
    case MNU_FILE_QUIT:  return FALSE;

    case MNU_VIEW_FIRST:
        if (st->current_page != 0) {
            st->current_page = 0;
            render_current_page(st); update_title(st); redraw(st);
        }
        break;
    case MNU_VIEW_LAST:
        if (st->current_page != st->page_count - 1) {
            st->current_page = st->page_count - 1;
            render_current_page(st); update_title(st); redraw(st);
        }
        break;
    case MNU_VIEW_PREV:
        if (st->current_page > 0) {
            st->current_page--;
            render_current_page(st); update_title(st); redraw(st);
        }
        break;
    case MNU_VIEW_NEXT:
        if (st->current_page < st->page_count - 1) {
            st->current_page++;
            render_current_page(st); update_title(st); redraw(st);
        }
        break;

    case MNU_VIEW_FIT_PAGE:
        st->zoom_mode = ZOOM_FIT_PAGE;
        render_current_page(st); update_title(st); redraw(st);
        break;
    case MNU_VIEW_FIT_WIDTH:
        st->zoom_mode = ZOOM_FIT_WIDTH;
        render_current_page(st); update_title(st); redraw(st);
        break;
    case MNU_VIEW_100:
        st->zoom_mode = ZOOM_CUSTOM;
        st->zoom_factor = 1.0f;
        render_current_page(st); update_title(st); redraw(st);
        break;
    case MNU_VIEW_ZOOM_IN:
        if (st->zoom_mode != ZOOM_CUSTOM) {
            /* Convert current fit to a concrete factor before nudging */
            st->zoom_mode = ZOOM_CUSTOM;
            st->zoom_factor = st->pix
                ? (float)st->pix->w /
                  (fz_bound_page(st->ctx,
                     fz_load_page(st->ctx, st->doc, st->current_page)).x1)
                : 1.0f;
        }
        st->zoom_factor *= 1.25f;
        render_current_page(st); update_title(st); redraw(st);
        break;
    case MNU_VIEW_ZOOM_OUT:
        if (st->zoom_mode != ZOOM_CUSTOM) {
            st->zoom_mode = ZOOM_CUSTOM;
            st->zoom_factor = 1.0f;
        }
        st->zoom_factor *= 0.8f;
        render_current_page(st); update_title(st); redraw(st);
        break;

    case MNU_HELP_ABOUT: {
        struct EasyStruct es = { sizeof(struct EasyStruct), 0,
            "About pdfview",
            "pdfview-os4 — a native PDF viewer for AmigaOS 4\n"
            "Built on MuPDF (Artifex) — https://mupdf.com\n"
            "Licensed under AGPL-3.0",
            "OK" };
        EasyRequest(st->win, &es, NULL);
        break;
    }
    }
    return TRUE;
}

int main(int argc, char *argv[])
{
    viewer_state st = {0};
    st.zoom_mode = ZOOM_FIT_PAGE;
    st.zoom_factor = 1.0f;

    st.ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
    if (!st.ctx) die("failed to init mupdf context");
    fz_register_document_handlers(st.ctx);

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
                          IDCMP_NEWSIZE | IDCMP_REFRESHWINDOW |
                          IDCMP_MENUPICK,
        TAG_END);
    if (!st.win) { UnlockPubScreen(NULL, screen); die("OpenWindow failed"); }

    /* Menu setup via gadtools.library — proto/gadtools.h gives us
     * the direct wrappers so we don't need to juggle the IGadTools
     * interface pointer manually. */
    struct Library *GadToolsBase = OpenLibrary("gadtools.library", 39);
    struct Menu *menu = NULL;
    APTR vi = NULL;
    if (GadToolsBase) {
        menu = CreateMenus(menu_data, GTMN_FullMenu, TRUE, TAG_END);
        if (menu) {
            vi = GetVisualInfo(screen, TAG_END);
            if (vi) {
                LayoutMenus(menu, vi, TAG_END);
                SetMenuStrip(st.win, menu);
            }
        }
    }

    /* If invoked with a file path, open it. */
    if (argc >= 2) open_document(&st, argv[1]);
    else update_title(&st);   /* empty title until user opens something */

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

            if (class == IDCMP_CLOSEWINDOW) { running = FALSE; break; }
            if (class == IDCMP_NEWSIZE) {
                render_current_page(&st); redraw(&st); continue;
            }
            if (class == IDCMP_REFRESHWINDOW) {
                BeginRefresh(st.win);
                redraw(&st);
                EndRefresh(st.win, TRUE);
                continue;
            }
            if (class == IDCMP_MENUPICK) {
                UWORD mnum = code;
                while (mnum != MENUNULL) {
                    struct MenuItem *itm = ItemAddress(st.win->MenuStrip, mnum);
                    if (!handle_menu(&st, mnum)) { running = FALSE; break; }
                    mnum = itm ? itm->NextSelect : MENUNULL;
                }
                continue;
            }
            if (class == IDCMP_RAWKEY && is_key_press && st.doc) {
                int new_page = st.current_page;
                zoom_mode_t new_zm = st.zoom_mode;
                float new_zf = st.zoom_factor;
                BOOL zoom_changed = FALSE;
                switch (keycode) {
                case RAWKEY_ESCAPE: running = FALSE; break;
                case RAWKEY_PAGEDOWN: case RAWKEY_SPACE:
                case RAWKEY_RIGHT:    case RAWKEY_DOWN:
                    if (new_page < st.page_count - 1) new_page++; break;
                case RAWKEY_PAGEUP:   case RAWKEY_BACKSPACE:
                case RAWKEY_LEFT:     case RAWKEY_UP:
                    if (new_page > 0) new_page--; break;
                case RAWKEY_HOME: new_page = 0; break;
                case RAWKEY_END:  new_page = st.page_count - 1; break;
                default: break;
                }
                if (new_page != st.current_page || zoom_changed) {
                    st.current_page = new_page;
                    st.zoom_mode = new_zm;
                    st.zoom_factor = new_zf;
                    render_current_page(&st);
                    update_title(&st);
                    redraw(&st);
                }
            }
        }
    }

    if (menu) { ClearMenuStrip(st.win); FreeMenus(menu); }
    if (vi) FreeVisualInfo(vi);
    if (GadToolsBase) CloseLibrary(GadToolsBase);
    CloseWindow(st.win);
    UnlockPubScreen(NULL, screen);
    if (st.pix) fz_drop_pixmap(st.ctx, st.pix);
    if (st.doc) fz_drop_document(st.ctx, st.doc);
    fz_drop_context(st.ctx);
    return 0;
}
