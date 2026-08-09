/*
 * net.c — URL fetching for TrapezePDF.
 *
 * Copyright (C) 2026  Chris Collins
 * Licensed under AGPL-3.0 (see LICENSE).
 *
 * Approach: shell out to the standalone `openssl` binary (AmiSSL 5.27+
 * CLI, typically at DH1:openssl or SYS:Utilities/openssl). Same pattern
 * python-amigaos4's amiga.https module uses. Advantages:
 *   - No compile-time SSL library dependency
 *   - Works with whatever AmiSSL certs the user has installed
 *   - Independent of newlib/clib4 differences
 *
 * Trade-offs:
 *   - Requires openssl binary on the guest (widely available for OS4)
 *   - Slower than a linked-library approach; fine for one-off fetches
 *   - No streaming — always fetches full document before opening
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#include "net.h"

/* Where we look for the openssl binary on the guest. First entry wins.
 * User can override with the OPENSSL_PATH env var. */
static const char *find_openssl_binary(void)
{
    const char *env = getenv("OPENSSL_PATH");
    if (env && *env) return env;

    /* Fallback candidates in likely install locations. Not verified —
     * openssl invocation will just fail cleanly if none exist. */
    return "DH1:openssl";
}

/* Ridiculously simple URL parser — handles just enough for our
 * fetch. Fills host/port/path (all caller-owned buffers). Returns 0 on
 * success, -1 on parse failure. Only http/https schemes recognized.
 *
 * Exposed as trapeze_parse_url for unit tests. Also kept as static
 * parse_url wrapper for the internal fetch path. */
int trapeze_parse_url(const char *url,
                      char *scheme, size_t scheme_sz,
                      char *host, size_t host_sz,
                      int *port,
                      char *path, size_t path_sz)
{
    const char *p = url;
    /* scheme */
    const char *colon = strstr(p, "://");
    if (!colon) return -1;
    size_t sl = (size_t)(colon - p);
    if (sl >= scheme_sz) return -1;
    memcpy(scheme, p, sl);
    scheme[sl] = '\0';
    for (size_t i = 0; scheme[i]; i++) scheme[i] = tolower(scheme[i]);

    p = colon + 3;

    /* host[:port] */
    const char *slash = strchr(p, '/');
    const char *hp_end = slash ? slash : p + strlen(p);
    const char *portcolon = memchr(p, ':', (size_t)(hp_end - p));
    size_t hlen = portcolon ? (size_t)(portcolon - p)
                            : (size_t)(hp_end - p);
    if (hlen == 0 || hlen >= host_sz) return -1;
    memcpy(host, p, hlen);
    host[hlen] = '\0';

    if (portcolon) {
        *port = atoi(portcolon + 1);
        if (*port <= 0 || *port > 65535) return -1;
    } else {
        *port = (strcmp(scheme, "https") == 0) ? 443 : 80;
    }

    /* path */
    if (slash) {
        size_t pl = strlen(slash);
        if (pl >= path_sz) return -1;
        strcpy(path, slash);
    } else {
        strcpy(path, "/");
    }
    return 0;
}

/* Write a minimal HTTP/1.0 request to req_file. Doesn't handle POST /
 * cookies / auth / redirects — we're just doing a GET. */
static int write_http_request(const char *req_file,
                              const char *host, int port, const char *path);
/* Non-static thunk so unit tests can call the static implementation
 * without pulling in the whole net.c compilation. */
int trapeze_write_http_request(const char *req_file,
                                const char *host, int port,
                                const char *path)
{
    return write_http_request(req_file, host, port, path);
}
static int write_http_request(const char *req_file,
                              const char *host, int port, const char *path)
{
    FILE *f = fopen(req_file, "wb");
    if (!f) return -1;
    fprintf(f, "GET %s HTTP/1.0\r\n", path);
    if (port == 443 || port == 80) {
        fprintf(f, "Host: %s\r\n", host);
    } else {
        fprintf(f, "Host: %s:%d\r\n", host, port);
    }
    fprintf(f,
        "User-Agent: TrapezePDF/1.0 (AmigaOS 4)\r\n"
        "Accept: application/pdf, */*\r\n"
        "Connection: close\r\n"
        "\r\n");
    fclose(f);
    return 0;
}

static int extract_http_body(const char *raw_file,
                             unsigned char **body_out, size_t *body_len_out);
/* Non-static thunk for the unit tests. */
int trapeze_extract_http_body(const char *raw_file,
                               unsigned char **body_out,
                               size_t *body_len_out)
{
    return extract_http_body(raw_file, body_out, body_len_out);
}
/* Strip an HTTP response prefix down to just the body. Returns 0 on
 * success (body_out/body_len_out filled with malloc'd data), non-zero
 * on failure. */
static int extract_http_body(const char *raw_file,
                             unsigned char **body_out, size_t *body_len_out)
{
    FILE *f = fopen(raw_file, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long total = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (total <= 0) { fclose(f); return -1; }

    unsigned char *buf = (unsigned char *)malloc((size_t)total);
    if (!buf) { fclose(f); return -1; }
    size_t got = fread(buf, 1, (size_t)total, f);
    fclose(f);
    if (got < 16) { free(buf); return -1; }

    /* Find "\r\n\r\n" separator between headers and body. Some openssl
     * builds emit their own connect banner before the actual HTTP
     * response — search for "HTTP/" first, then the header terminator
     * after that. */
    unsigned char *http = (unsigned char *)memmem(buf, got, "HTTP/", 5);
    unsigned char *scan = http ? http : buf;
    size_t remain = got - (size_t)(scan - buf);
    int sep_len = 4;
    unsigned char *sep = (unsigned char *)memmem(scan, remain, "\r\n\r\n", 4);
    if (!sep) {
        sep = (unsigned char *)memmem(scan, remain, "\n\n", 2);
        sep_len = 2;
    }
    if (!sep) { free(buf); return -1; }

    size_t hdr_len = (size_t)(sep - scan) + sep_len;
    unsigned char *body = scan + hdr_len;
    size_t body_len = remain - hdr_len;

    /* If Content-Length: is present in the response headers, trust it
     * — openssl s_client's TLS session-info output can end up appended
     * to our captured stdout on OS4, so an unbounded body would carry
     * cert-chain text past the PDF's %%EOF. Case-insensitive scan
     * limited to the header region. */
    for (size_t i = 0; i + 15 < hdr_len; i++) {
        if ((scan[i] == 'C' || scan[i] == 'c') &&
            strncasecmp((const char *)(scan + i), "Content-Length:", 15) == 0)
        {
            size_t j = i + 15;
            while (j < hdr_len && (scan[j] == ' ' || scan[j] == '\t')) j++;
            long declared = 0;
            while (j < hdr_len && scan[j] >= '0' && scan[j] <= '9') {
                declared = declared * 10 + (scan[j] - '0');
                j++;
            }
            if (declared > 0 && (size_t)declared < body_len) {
                body_len = (size_t)declared;
            }
            break;
        }
    }

    /* Move body to the start of buf so caller can free the one pointer. */
    memmove(buf, body, body_len);
    unsigned char *shrunk = (unsigned char *)realloc(buf, body_len);
    *body_out = shrunk ? shrunk : buf;
    *body_len_out = body_len;
    return 0;
}

/* Diagnostic log — written to a fixed path so it survives whatever
 * stdout/stderr redirection the shell did (or didn't do). Any URL
 * fetch failure diagnosis starts here. */
#define TRAPEZE_LOG_PATH  "T:trapeze_fetch.log"
static FILE *g_log = NULL;
static void log_open(void)  { g_log = fopen(TRAPEZE_LOG_PATH, "w"); }
static void log_close(void) { if (g_log) { fclose(g_log); g_log = NULL; } }
#define LOG(...) do {                                              \
    fprintf(stderr, __VA_ARGS__);                                  \
    if (g_log) { fprintf(g_log, __VA_ARGS__); fflush(g_log); }     \
} while (0)

/* Fetch a URL, save to `out_path`. Returns 0 on success. */
int fetch_url(const char *url, const char *out_path)
{
    log_open();
    LOG("TrapezePDF: fetch_url(%s -> %s)\n", url, out_path);

    char scheme[16], host[256], path[1024];
    int port;
    if (trapeze_parse_url(url, scheme, sizeof(scheme), host, sizeof(host),
                           &port, path, sizeof(path)) != 0) {
        LOG("TrapezePDF: URL parse failed: %s\n", url);
        log_close();
        return -1;
    }
    LOG("TrapezePDF: parsed scheme=%s host=%s port=%d path=%s\n",
        scheme, host, port, path);
    if (strcmp(scheme, "http") != 0 && strcmp(scheme, "https") != 0) {
        LOG("TrapezePDF: only http/https URLs supported\n");
        log_close();
        return -1;
    }

    const char *req_file = "T:trapeze_req";
    const char *raw_file = "T:trapeze_raw";
    /* Wipe any leftover file so we can tell if openssl wrote a fresh one. */
    remove(raw_file);
    if (write_http_request(req_file, host, port, path) != 0) {
        LOG("TrapezePDF: cannot write request file %s\n", req_file);
        log_close();
        return -1;
    }

    /* AmigaDOS pipe: `type reqfile | openssl s_client ...` sends the
     * HTTP request to openssl's stdin. `<file` redirect doesn't work
     * in AmigaDOS shell — you must pipe via type. */
    char cmd[2048];
    const char *ossl = find_openssl_binary();
    if (strcmp(scheme, "https") == 0) {
        snprintf(cmd, sizeof(cmd),
            "type %s | %s s_client -connect %s:%d -servername %s "
            "-quiet -CApath DH1:AmiSSL/Certs >%s",
            req_file, ossl, host, port, host, raw_file);
    } else {
        LOG("TrapezePDF: plain http:// not supported yet\n");
        log_close();
        return -1;
    }

    LOG("TrapezePDF: cmd=%s\n", cmd);
    int rc = system(cmd);
    LOG("TrapezePDF: system() rc=%d\n", rc);

    /* Report raw_file size before parsing. */
    FILE *rf = fopen(raw_file, "rb");
    if (!rf) {
        LOG("TrapezePDF: %s does not exist after system() — openssl not "
            "found or ran with no output\n", raw_file);
    } else {
        fseek(rf, 0, SEEK_END);
        long sz = ftell(rf);
        fclose(rf);
        LOG("TrapezePDF: %s size = %ld bytes\n", raw_file, sz);
    }

    unsigned char *body = NULL;
    size_t body_len = 0;
    if (extract_http_body(raw_file, &body, &body_len) != 0) {
        LOG("TrapezePDF: no HTTP response body extracted "
            "(openssl at %s produced no valid output)\n", ossl);
        remove(req_file);
        remove(raw_file);
        log_close();
        return -1;
    }
    remove(req_file);
    remove(raw_file);

    FILE *out = fopen(out_path, "wb");
    if (!out) {
        LOG("TrapezePDF: cannot write %s\n", out_path);
        free(body);
        log_close();
        return -1;
    }
    fwrite(body, 1, body_len, out);
    fclose(out);
    free(body);

    LOG("TrapezePDF: downloaded %zu bytes to %s\n", body_len, out_path);
    log_close();
    return 0;
}

int is_url(const char *s)
{
    if (!s || !*s) return 0;
    return (strncmp(s, "http://", 7) == 0 ||
            strncmp(s, "https://", 8) == 0);
}
