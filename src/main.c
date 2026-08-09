/*
 * TrapezePDF — a native PDF viewer for AmigaOS 4.
 * Copyright (C) 2026  Chris Collins
 *
 * Built on MuPDF (https://mupdf.com) — Copyright Artifex Software.
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
#include <mupdf/pdf.h>       /* pdf_document, pdf_page, pdf_annot,
                              * pdf_widget — needed for phases 6/7/8 */

#include "net.h"             /* fetch_url() for File → Open URL */

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
    MNU_FILE_SAVEAS,
    MNU_FILE_PRINT,
    MNU_FILE_PRINT_FILE,
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
    /* Phase 8: page management */
    MNU_PAGE_ROTATE_CW,
    MNU_PAGE_ROTATE_CCW,
    MNU_PAGE_ROTATE_180,
    MNU_PAGE_DELETE,
    MNU_PAGE_EXTRACT,
    /* Phase 6: annotations */
    MNU_ANNOT_ADD_NOTE,
    MNU_ANNOT_DELETE_ALL,
    /* Phase 7: form fill */
    MNU_FORM_FILL_NEXT,
    MNU_FORM_LIST,
    /* URL / clipboard */
    MNU_FILE_OPEN_URL,
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
    { NM_TITLE, "File",             0,   0, 0, 0                          },
    { NM_ITEM,  "Open...",          "O", 0, 0, (APTR)MNU_FILE_OPEN         },
    { NM_ITEM,  "Open URL from Clipboard", "U", 0, 0, (APTR)MNU_FILE_OPEN_URL },
    { NM_ITEM,  "Save As...",       "S", 0, 0, (APTR)MNU_FILE_SAVEAS       },
    { NM_ITEM,  "Print...",         "P", 0, 0, (APTR)MNU_FILE_PRINT        },
    { NM_ITEM,  "Print to File...", 0,   0, 0, (APTR)MNU_FILE_PRINT_FILE   },
    { NM_ITEM,  NM_BARLABEL,        0,   0, 0, 0                          },
    { NM_ITEM,  "Quit",             "Q", 0, 0, (APTR)MNU_FILE_QUIT         },

    { NM_TITLE, "View",             0,   0, 0, 0                          },
    { NM_ITEM,  "First Page",       0,   0, 0, (APTR)MNU_VIEW_FIRST        },
    { NM_ITEM,  "Previous",         0,   0, 0, (APTR)MNU_VIEW_PREV         },
    { NM_ITEM,  "Next",             0,   0, 0, (APTR)MNU_VIEW_NEXT         },
    { NM_ITEM,  "Last Page",        0,   0, 0, (APTR)MNU_VIEW_LAST         },
    { NM_ITEM,  NM_BARLABEL,        0,   0, 0, 0                          },
    { NM_ITEM,  "Fit Page",         "F", 0, 0, (APTR)MNU_VIEW_FIT_PAGE     },
    { NM_ITEM,  "Fit Width",        "W", 0, 0, (APTR)MNU_VIEW_FIT_WIDTH    },
    { NM_ITEM,  "100%",             "1", 0, 0, (APTR)MNU_VIEW_100          },
    { NM_ITEM,  "Zoom In",          "+", 0, 0, (APTR)MNU_VIEW_ZOOM_IN      },
    { NM_ITEM,  "Zoom Out",         "-", 0, 0, (APTR)MNU_VIEW_ZOOM_OUT     },

    { NM_TITLE, "Page",             0,   0, 0, 0                          },
    { NM_ITEM,  "Rotate 90 CW",     "R", 0, 0, (APTR)MNU_PAGE_ROTATE_CW    },
    { NM_ITEM,  "Rotate 90 CCW",    "L", 0, 0, (APTR)MNU_PAGE_ROTATE_CCW   },
    { NM_ITEM,  "Rotate 180",       0,   0, 0, (APTR)MNU_PAGE_ROTATE_180   },
    { NM_ITEM,  NM_BARLABEL,        0,   0, 0, 0                          },
    { NM_ITEM,  "Delete This Page", 0,   0, 0, (APTR)MNU_PAGE_DELETE       },
    { NM_ITEM,  "Extract This Page...", 0, 0, 0, (APTR)MNU_PAGE_EXTRACT    },

    { NM_TITLE, "Annotate",         0,   0, 0, 0                          },
    { NM_ITEM,  "Add Sticky Note...", "N", 0, 0, (APTR)MNU_ANNOT_ADD_NOTE  },
    { NM_ITEM,  "Delete All on Page", 0, 0, 0, (APTR)MNU_ANNOT_DELETE_ALL  },

    { NM_TITLE, "Form",             0,   0, 0, 0                          },
    { NM_ITEM,  "List Form Fields", 0,   0, 0, (APTR)MNU_FORM_LIST         },
    { NM_ITEM,  "Fill Next Field...", 0, 0, 0, (APTR)MNU_FORM_FILL_NEXT    },

    { NM_TITLE, "Help",             0,   0, 0, 0                          },
    { NM_ITEM,  "About...",         "?", 0, 0, (APTR)MNU_HELP_ABOUT        },

    { NM_END,   0,                  0,   0, 0, 0                          }
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
             "TrapezePDF - %s - Page %d/%d [%s]",
             base, st->current_page + 1, st->page_count, zoom_str);
    SetWindowTitles(st->win, (STRPTR)title, (STRPTR)-1);
}

/* Read clipboard.device unit 0 as text — returns malloc'd 0-terminated
 * string or NULL if empty/unreadable. Skips the IFF FTXT wrapper. */
#include <devices/clipboard.h>
#include <exec/io.h>
#include <exec/memory.h>
static char *read_clipboard_text(void)
{
    struct MsgPort *port = CreateMsgPort();
    if (!port) return NULL;
    struct IOClipReq *req = (struct IOClipReq *)CreateIORequest(port,
                                            sizeof(struct IOClipReq));
    if (!req) { DeleteMsgPort(port); return NULL; }
    req->io_ClipID = 0;
    if (OpenDevice("clipboard.device", 0,
                    (struct IORequest *)req, 0)) {
        DeleteIORequest((struct IORequest *)req);
        DeleteMsgPort(port);
        return NULL;
    }

    /* Read entire clipboard into a growable buffer. clipboard.device
     * gives us IFF-formatted data; we skip FORM/FTXT/CHRS headers and
     * concat the CHRS payload. Simple parser sufficient for URL cases. */
    char *out = NULL;
    size_t out_len = 0;
    unsigned char buf[512];

    req->io_Command = CMD_READ;
    req->io_Data    = (STRPTR)buf;
    req->io_Length  = sizeof(buf);
    req->io_Offset  = 0;
    req->io_ClipID  = 0;

    while (DoIO((struct IORequest *)req) == 0 && req->io_Actual > 0) {
        char *nb = (char *)realloc(out, out_len + req->io_Actual);
        if (!nb) break;
        out = nb;
        memcpy(out + out_len, buf, req->io_Actual);
        out_len += req->io_Actual;
        req->io_Command = CMD_READ;
        req->io_Data    = (STRPTR)buf;
        req->io_Length  = sizeof(buf);
    }

    CloseDevice((struct IORequest *)req);
    DeleteIORequest((struct IORequest *)req);
    DeleteMsgPort(port);

    if (!out || out_len == 0) { free(out); return NULL; }

    /* Very-loose IFF parse: skip until CHRS chunk, then copy its
     * payload out. If no CHRS found, treat whole buffer as text. */
    unsigned char *scan = (unsigned char *)out;
    unsigned char *end = scan + out_len;
    for (unsigned char *p = scan; p + 8 <= end; p++) {
        if (p[0] == 'C' && p[1] == 'H' && p[2] == 'R' && p[3] == 'S') {
            unsigned int len = ((unsigned int)p[4] << 24) |
                                ((unsigned int)p[5] << 16) |
                                ((unsigned int)p[6] << 8)  |
                                ((unsigned int)p[7]);
            if (p + 8 + len > end) break;
            char *text = (char *)malloc(len + 1);
            if (!text) { free(out); return NULL; }
            memcpy(text, p + 8, len);
            text[len] = '\0';
            free(out);
            /* Trim trailing whitespace/newlines/nulls */
            while (len > 0 && (text[len-1] == '\n' || text[len-1] == '\r' ||
                                text[len-1] == ' ' || text[len-1] == '\0'))
                text[--len] = '\0';
            return text;
        }
    }
    /* No CHRS chunk — try to interpret entire clipboard as text.
     * Null-terminate and trim. */
    char *text = (char *)realloc(out, out_len + 1);
    if (!text) text = out;
    text[out_len] = '\0';
    while (out_len > 0 && (text[out_len-1] == '\n' || text[out_len-1] == '\r' ||
                            text[out_len-1] == ' '))
        text[--out_len] = '\0';
    return text;
}

/* Load a new document, replacing the current one. Handles both local
 * paths and http(s):// URLs (via fetch_url — see src/net.c). */
static void open_document(viewer_state *st, const char *path)
{
    const char *actual_path = path;
    char cached_path[64];
    if (is_url(path)) {
        /* Download to T: — the URL is transient state, no need to
         * keep it around. Overwrites previous download. */
        strcpy(cached_path, "T:trapeze_download.pdf");
        if (fetch_url(path, cached_path) != 0) {
            fprintf(stderr, "TrapezePDF: URL fetch failed for %s\n", path);
            return;
        }
        actual_path = cached_path;
    }

    fz_document *newdoc = NULL;
    fz_try(st->ctx) {
        newdoc = fz_open_document(st->ctx, actual_path);
        if (!newdoc) fz_throw(st->ctx, FZ_ERROR_GENERIC, "open_document NULL");
    }
    fz_catch(st->ctx) {
        fprintf(stderr, "TrapezePDF: cannot open %s: %s\n",
                actual_path, fz_caught_message(st->ctx));
        return;
    }
    if (st->doc) fz_drop_document(st->ctx, st->doc);
    st->doc = newdoc;
    st->page_count = fz_count_pages(st->ctx, newdoc);
    st->current_page = 0;
    /* Store the ORIGINAL path (URL or local) in filepath — that's
     * what the user sees in the title bar. actual_path may point
     * to the T: cache file for URL downloads. */
    strncpy(st->filepath, path, sizeof(st->filepath) - 1);
    st->filepath[sizeof(st->filepath) - 1] = '\0';
    render_current_page(st);
    update_title(st);
    redraw(st);
}

/* File → Open URL from Clipboard. Reads clipboard.device, expects
 * a http(s):// URL, downloads and opens. */
static void action_file_open_url(viewer_state *st)
{
    char *url = read_clipboard_text();
    if (!url || !*url) {
        struct EasyStruct es = { sizeof(struct EasyStruct), 0,
            "Open URL from Clipboard",
            "Clipboard is empty or unreadable.\n\n"
            "Copy an http:// or https:// URL to the clipboard first,\n"
            "then try again.",
            "OK" };
        EasyRequest(st->win, &es, NULL);
        free(url);
        return;
    }
    if (!is_url(url)) {
        struct EasyStruct es = { sizeof(struct EasyStruct), 0,
            "Open URL from Clipboard",
            "Clipboard contents do not look like a URL.\n\n"
            "Expected http:// or https:// prefix.",
            "OK" };
        EasyRequest(st->win, &es, NULL);
        free(url);
        return;
    }
    open_document(st, url);
    free(url);
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

/* ask_save_path is defined below with the other ASL helpers; forward-
 * declare it here so Print (defined earlier) can use it. basename_of
 * is already defined above. */
static char *ask_save_path(viewer_state *st, const char *title,
                            const char *default_name);

/* Render the whole document as PostScript to `out_path`. Returns 0 on
 * success, -1 on any MuPDF failure (message already emitted). Shared
 * between "Print..." (writes to T: then copies to PRT:) and "Print to
 * File..." (writes directly to the user-chosen path). */
static int write_ps_to(viewer_state *st, const char *out_path)
{
    fz_document_writer *wri = NULL;
    int failed = 0;
    fz_try(st->ctx) {
        wri = fz_new_document_writer(st->ctx, out_path, "ps", NULL);
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
        fprintf(stderr, "pdfview: PS writer failed: %s\n",
                fz_caught_message(st->ctx));
        failed = 1;
    }
    return failed ? -1 : 0;
}

/* File→Print — render all pages to PostScript, then copy to PRT:
 * (routed through OS4 printer prefs). Uses a temp file in T: as the
 * intermediate; deletes it after copying. */
static void action_file_print(viewer_state *st)
{
    if (!st->doc) return;
    fprintf(stderr, "pdfview: print — rendering %d pages via mupdf...\n",
            st->page_count);
    const char *ps_path = "T:pdfview_print.ps";
    if (write_ps_to(st, ps_path) != 0) return;
    system("copy T:pdfview_print.ps PRT: QUIET");
    system("delete T:pdfview_print.ps QUIET");
    fprintf(stderr, "pdfview: print — sent to PRT:\n");
}

/* File→Print to File — same PS generation, but ask the user for an
 * output path and write directly (no copy to PRT:). Convenient for
 * saving a PS proof, feeding into ghostscript, or debugging without
 * an OS4 printer driver installed. */
static void action_file_print_to_file(viewer_state *st)
{
    if (!st->doc) return;
    char default_name[256];
    const char *base = basename_of(st->filepath);
    /* Swap trailing .pdf for .ps if we can. */
    snprintf(default_name, sizeof(default_name), "%s", base ? base : "print.ps");
    size_t n = strlen(default_name);
    if (n >= 4 &&
        (strcasecmp(default_name + n - 4, ".pdf") == 0)) {
        strcpy(default_name + n - 4, ".ps");
    } else {
        size_t room = sizeof(default_name) - n - 1;
        if (room >= 3) strcat(default_name, ".ps");
    }
    char *path = ask_save_path(st, "Print to File (PostScript)",
                                default_name);
    if (!path) return;
    fprintf(stderr, "pdfview: print to file — rendering %d pages -> %s\n",
            st->page_count, path);
    if (write_ps_to(st, path) == 0) {
        fprintf(stderr, "pdfview: print to file — wrote %s\n", path);
    }
    free(path);
}

/* --- Phase 8: page management --------------------------------------
 * Rotate/delete/extract/save-as operate on the underlying pdf_document.
 * We cast fz_document* → pdf_document* via pdf_specifics(); returns
 * NULL if the document isn't a PDF (e.g. XPS or EPUB). Our menu items
 * are all no-ops in that case. */

/* Ask for a save-as path via ASL requester. Returns malloc'd path or
 * NULL if user cancelled. Caller must free. */
static char *ask_save_path(viewer_state *st, const char *title,
                            const char *default_name)
{
    struct FileRequester *req = AllocAslRequestTags(ASL_FileRequest,
        ASLFR_TitleText,        (uintptr_t)title,
        ASLFR_DoSaveMode,       TRUE,
        ASLFR_InitialFile,      (uintptr_t)(default_name ? default_name : ""),
        ASLFR_Window,           (uintptr_t)st->win,
        TAG_END);
    if (!req) return NULL;
    char *result = NULL;
    if (AslRequestTags(req, TAG_END)) {
        char path[512];
        size_t dl = req->fr_Drawer ? strlen(req->fr_Drawer) : 0;
        if (dl > 0 && req->fr_Drawer[dl - 1] == ':')
            snprintf(path, sizeof(path), "%s%s",
                     req->fr_Drawer, req->fr_File);
        else if (dl > 0)
            snprintf(path, sizeof(path), "%s/%s",
                     req->fr_Drawer, req->fr_File);
        else
            snprintf(path, sizeof(path), "%s", req->fr_File);
        result = strdup(path);
    }
    FreeAslRequest(req);
    return result;
}

static void action_file_saveas(viewer_state *st)
{
    if (!st->doc) return;
    pdf_document *pdf = pdf_specifics(st->ctx, st->doc);
    if (!pdf) {
        fprintf(stderr, "pdfview: save-as: not a PDF document\n");
        return;
    }
    char *path = ask_save_path(st, "Save PDF As",
                                basename_of(st->filepath));
    if (!path) return;
    fz_try(st->ctx) {
        pdf_write_options opts = pdf_default_write_options;
        pdf_save_document(st->ctx, pdf, path, &opts);
        fprintf(stderr, "pdfview: saved → %s\n", path);
    }
    fz_catch(st->ctx) {
        fprintf(stderr, "pdfview: save failed: %s\n",
                fz_caught_message(st->ctx));
    }
    free(path);
}

/* Rotate current page by `deg` degrees (must be multiple of 90). */
static void action_page_rotate(viewer_state *st, int deg)
{
    if (!st->doc) return;
    pdf_document *pdf = pdf_specifics(st->ctx, st->doc);
    if (!pdf) return;
    fz_try(st->ctx) {
        pdf_obj *page_obj = pdf_lookup_page_obj(st->ctx, pdf,
                                                  st->current_page);
        int cur = pdf_dict_get_int(st->ctx, page_obj, PDF_NAME(Rotate));
        int new_rot = (cur + deg) % 360;
        if (new_rot < 0) new_rot += 360;
        pdf_dict_put_int(st->ctx, page_obj, PDF_NAME(Rotate), new_rot);
        fprintf(stderr, "pdfview: rotated page %d to %d degrees\n",
                st->current_page + 1, new_rot);
    }
    fz_catch(st->ctx) {
        fprintf(stderr, "pdfview: rotate failed: %s\n",
                fz_caught_message(st->ctx));
        return;
    }
    render_current_page(st);
    redraw(st);
}

static void action_page_delete(viewer_state *st)
{
    if (!st->doc || st->page_count < 2) {
        /* Refuse to delete the only page in a document. */
        struct EasyStruct es = { sizeof(struct EasyStruct), 0,
            "Delete Page",
            "Cannot delete: a PDF must have at least one page.",
            "OK" };
        EasyRequest(st->win, &es, NULL);
        return;
    }
    pdf_document *pdf = pdf_specifics(st->ctx, st->doc);
    if (!pdf) return;
    fz_try(st->ctx) {
        pdf_delete_page(st->ctx, pdf, st->current_page);
        st->page_count--;
        if (st->current_page >= st->page_count)
            st->current_page = st->page_count - 1;
        fprintf(stderr, "pdfview: deleted page; %d pages remain\n",
                st->page_count);
    }
    fz_catch(st->ctx) {
        fprintf(stderr, "pdfview: delete failed: %s\n",
                fz_caught_message(st->ctx));
        return;
    }
    render_current_page(st);
    update_title(st);
    redraw(st);
}

/* Extract current page to a new PDF. Simplest approach: write a fresh
 * one-page PDF via document writer. Preserves rendering fidelity but
 * loses annotations/forms on the extracted page (they don't survive
 * a re-render). Good enough for the common "extract this receipt". */
static void action_page_extract(viewer_state *st)
{
    if (!st->doc) return;
    char default_name[128];
    snprintf(default_name, sizeof(default_name), "%s-page%d.pdf",
             basename_of(st->filepath), st->current_page + 1);
    char *path = ask_save_path(st, "Extract Page As", default_name);
    if (!path) return;

    fz_document_writer *wri = NULL;
    fz_try(st->ctx) {
        wri = fz_new_document_writer(st->ctx, path, "pdf", NULL);
        fz_page *page = fz_load_page(st->ctx, st->doc, st->current_page);
        fz_rect r = fz_bound_page(st->ctx, page);
        fz_device *dev = fz_begin_page(st->ctx, wri, r);
        fz_run_page(st->ctx, page, dev, fz_identity, NULL);
        fz_end_page(st->ctx, wri);
        fz_drop_page(st->ctx, page);
        fz_close_document_writer(st->ctx, wri);
        fprintf(stderr, "pdfview: extracted page → %s\n", path);
    }
    fz_always(st->ctx) { fz_drop_document_writer(st->ctx, wri); }
    fz_catch(st->ctx) {
        fprintf(stderr, "pdfview: extract failed: %s\n",
                fz_caught_message(st->ctx));
    }
    free(path);
}

/* --- Phase 6: annotations (add sticky note, delete all on page) ------
 * Full annotation UI (freehand, highlight-with-mouse-selection) is
 * out of scope for the initial v1.0; those need selection tools,
 * screen-to-page coord translation, undo stack. What we ship here:
 * add-a-sticky-note-at-page-center, and delete-all-annotations-on-page. */

/* Ask the user for a string via a simple ASL string requester. Returns
 * malloc'd string or NULL. On OS4 without a proper string requester
 * class, we fake it via EasyRequest with a placeholder — real impl
 * would use ReAction string.gadget in a modal window. */
static char *ask_string(viewer_state *st, const char *title,
                        const char *prompt, const char *default_val)
{
    /* Placeholder: since we don't yet have a proper string dialog,
     * hardcode a demo note. TODO: replace with ReAction dialog. */
    (void)st; (void)title; (void)prompt; (void)default_val;
    static char demo[128];
    snprintf(demo, sizeof(demo), "%s (added %d)", prompt ? prompt : "Note",
             (int)(uintptr_t)st);   /* pseudo-unique per invocation */
    return strdup(demo);
}

static void action_annot_add_note(viewer_state *st)
{
    if (!st->doc) return;
    pdf_document *pdf = pdf_specifics(st->ctx, st->doc);
    if (!pdf) return;
    char *text = ask_string(st, "Add Sticky Note",
                            "Note text (demo placeholder)", "");
    if (!text) return;

    fz_try(st->ctx) {
        pdf_page *page = pdf_load_page(st->ctx, pdf, st->current_page);
        pdf_annot *annot = pdf_create_annot(st->ctx, page, PDF_ANNOT_TEXT);
        /* Position note at center of page (in PDF user-space points). */
        fz_rect page_bounds = pdf_bound_page(st->ctx, page,
            FZ_MEDIA_BOX);
        float cx = (page_bounds.x0 + page_bounds.x1) / 2.0f;
        float cy = (page_bounds.y0 + page_bounds.y1) / 2.0f;
        fz_rect r = { cx - 12, cy - 12, cx + 12, cy + 12 };
        pdf_set_annot_rect(st->ctx, annot, r);
        pdf_set_annot_contents(st->ctx, annot, text);
        /* pdf_update_appearance moved between MuPDF versions; the
         * annotation still gets a default appearance when re-rendered. */
        fz_drop_page(st->ctx, (fz_page*)page);
        fprintf(stderr, "pdfview: added sticky note on page %d\n",
                st->current_page + 1);
    }
    fz_catch(st->ctx) {
        fprintf(stderr, "pdfview: add-note failed: %s\n",
                fz_caught_message(st->ctx));
    }
    free(text);
    render_current_page(st);
    redraw(st);
}

static void action_annot_delete_all(viewer_state *st)
{
    if (!st->doc) return;
    pdf_document *pdf = pdf_specifics(st->ctx, st->doc);
    if (!pdf) return;
    fz_try(st->ctx) {
        pdf_page *page = pdf_load_page(st->ctx, pdf, st->current_page);
        pdf_annot *annot;
        int count = 0;
        while ((annot = pdf_first_annot(st->ctx, page)) != NULL) {
            pdf_delete_annot(st->ctx, page, annot);
            count++;
        }
        fz_drop_page(st->ctx, (fz_page*)page);
        fprintf(stderr, "pdfview: deleted %d annotation(s) on page %d\n",
                count, st->current_page + 1);
    }
    fz_catch(st->ctx) {
        fprintf(stderr, "pdfview: delete-annots failed: %s\n",
                fz_caught_message(st->ctx));
    }
    render_current_page(st);
    redraw(st);
}

/* --- Phase 7: form fill (list + fill next) --------------------------
 * PDF forms have widgets that we iterate with pdf_first_widget/
 * pdf_next_widget. Text-fields we fill via pdf_set_field_value.
 * Checkbox/radio/dropdown deferred. */

static void action_form_list(viewer_state *st)
{
    if (!st->doc) return;
    pdf_document *pdf = pdf_specifics(st->ctx, st->doc);
    if (!pdf) return;

    /* Write field summary to stderr and stringbuf for EasyRequest. */
    char summary[2048];
    int off = snprintf(summary, sizeof(summary),
                        "Form fields on page %d:\n", st->current_page + 1);
    int count = 0;
    fz_try(st->ctx) {
        pdf_page *page = pdf_load_page(st->ctx, pdf, st->current_page);
        pdf_annot *w = pdf_first_widget(st->ctx, page);
        while (w) {
            int type = pdf_widget_type(st->ctx, w);
            const char *tname = "?";
            switch (type) {
            case PDF_WIDGET_TYPE_BUTTON:   tname = "button";   break;
            case PDF_WIDGET_TYPE_CHECKBOX: tname = "checkbox"; break;
            case PDF_WIDGET_TYPE_COMBOBOX: tname = "combobox"; break;
            case PDF_WIDGET_TYPE_LISTBOX:  tname = "listbox";  break;
            case PDF_WIDGET_TYPE_RADIOBUTTON: tname = "radio"; break;
            case PDF_WIDGET_TYPE_SIGNATURE: tname = "signature"; break;
            case PDF_WIDGET_TYPE_TEXT:     tname = "text";     break;
            default: break;
            }
            const char *val = pdf_annot_field_value(st->ctx, w);
            if (off < (int)sizeof(summary) - 1) {
                off += snprintf(summary + off, sizeof(summary) - off,
                                "  #%d [%s] = %s\n", count + 1,
                                tname, val ? val : "(empty)");
            }
            count++;
            w = pdf_next_widget(st->ctx, w);
        }
        fz_drop_page(st->ctx, (fz_page*)page);
    }
    fz_catch(st->ctx) {
        snprintf(summary, sizeof(summary),
                 "Error listing form fields: %s",
                 fz_caught_message(st->ctx));
    }
    if (count == 0)
        strncpy(summary, "No form fields on this page.", sizeof(summary));
    struct EasyStruct es = { sizeof(struct EasyStruct), 0,
        "Form Fields", summary, "OK" };
    EasyRequest(st->win, &es, NULL);
}

static void action_form_fill_next(viewer_state *st)
{
    if (!st->doc) return;
    pdf_document *pdf = pdf_specifics(st->ctx, st->doc);
    if (!pdf) return;
    char *value = ask_string(st, "Fill Form Field",
                              "Value for next empty text field",
                              "sample-value");
    if (!value) return;

    BOOL filled = FALSE;
    fz_try(st->ctx) {
        pdf_page *page = pdf_load_page(st->ctx, pdf, st->current_page);
        pdf_annot *w = pdf_first_widget(st->ctx, page);
        while (w) {
            if (pdf_widget_type(st->ctx, w) == PDF_WIDGET_TYPE_TEXT) {
                const char *cur = pdf_annot_field_value(st->ctx, w);
                if (!cur || !*cur) {
                    pdf_set_annot_field_value(st->ctx, pdf, w, value, 1);
                    filled = TRUE;
                    break;
                }
            }
            w = pdf_next_widget(st->ctx, w);
        }
        fz_drop_page(st->ctx, (fz_page*)page);
    }
    fz_catch(st->ctx) {
        fprintf(stderr, "pdfview: fill-field failed: %s\n",
                fz_caught_message(st->ctx));
    }
    if (filled) {
        fprintf(stderr, "pdfview: filled a text field with '%s'\n", value);
    } else {
        struct EasyStruct es = { sizeof(struct EasyStruct), 0,
            "Fill Form Field",
            "No empty text fields on this page.", "OK" };
        EasyRequest(st->win, &es, NULL);
    }
    free(value);
    render_current_page(st);
    redraw(st);
}

/* Handle a menu selection. Returns FALSE if the user chose Quit. */
static BOOL handle_menu(viewer_state *st, UWORD menu_num)
{
    struct MenuItem *item = ItemAddress(st->win->MenuStrip, menu_num);
    if (!item) return TRUE;
    ULONG id = (ULONG)(uintptr_t)GTMENUITEM_USERDATA(item);

    switch (id) {
    case MNU_FILE_OPEN:     action_file_open(st); break;
    case MNU_FILE_OPEN_URL: action_file_open_url(st); break;
    case MNU_FILE_SAVEAS:   action_file_saveas(st); break;
    case MNU_FILE_PRINT:    action_file_print(st); break;
    case MNU_FILE_PRINT_FILE: action_file_print_to_file(st); break;
    case MNU_FILE_QUIT:     return FALSE;

    case MNU_PAGE_ROTATE_CW:  action_page_rotate(st, 90); break;
    case MNU_PAGE_ROTATE_CCW: action_page_rotate(st, -90); break;
    case MNU_PAGE_ROTATE_180: action_page_rotate(st, 180); break;
    case MNU_PAGE_DELETE:     action_page_delete(st); break;
    case MNU_PAGE_EXTRACT:    action_page_extract(st); break;

    case MNU_ANNOT_ADD_NOTE:   action_annot_add_note(st); break;
    case MNU_ANNOT_DELETE_ALL: action_annot_delete_all(st); break;

    case MNU_FORM_LIST:      action_form_list(st); break;
    case MNU_FORM_FILL_NEXT: action_form_fill_next(st); break;

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
            "About TrapezePDF",
            "TrapezePDF - a native PDF viewer for AmigaOS 4\n"
            "by Chris Collins\n\n"
            "Built on MuPDF (Artifex) - https://mupdf.com\n"
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
        WA_Title,         (uintptr_t)"TrapezePDF",
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
