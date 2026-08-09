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
                              const char *host, int port, const char *path)
{
    FILE *f = fopen(req_file, "wb");
    if (!f) return -1;
    fprintf(f,
        "GET %s HTTP/1.0\r\n"
        "Host: %s%s%d\r\n"
        "User-Agent: TrapezePDF/1.0 (AmigaOS 4)\r\n"
        "Accept: application/pdf, */*\r\n"
        "Connection: close\r\n"
        "\r\n",
        path, host,
        (port == 443 || port == 80) ? "" : ":",
        (port == 443 || port == 80) ? 0 : port);
    fclose(f);
    return 0;
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
    unsigned char *sep = (unsigned char *)memmem(scan, remain, "\r\n\r\n", 4);
    if (!sep) sep = (unsigned char *)memmem(scan, remain, "\n\n", 2);
    if (!sep) { free(buf); return -1; }

    size_t hdr_len = (size_t)(sep - scan) +
                     (sep[1] == '\n' ? 2 : 4);
    unsigned char *body = scan + hdr_len;
    size_t body_len = remain - hdr_len;

    /* Move body to the start of buf so caller can free the one pointer. */
    memmove(buf, body, body_len);
    unsigned char *shrunk = (unsigned char *)realloc(buf, body_len);
    *body_out = shrunk ? shrunk : buf;
    *body_len_out = body_len;
    return 0;
}

/* Fetch a URL, save to `out_path`. Returns 0 on success. */
int fetch_url(const char *url, const char *out_path)
{
    char scheme[16], host[256], path[1024];
    int port;
    if (trapeze_parse_url(url, scheme, sizeof(scheme), host, sizeof(host),
                           &port, path, sizeof(path)) != 0) {
        fprintf(stderr, "TrapezePDF: URL parse failed: %s\n", url);
        return -1;
    }
    if (strcmp(scheme, "http") != 0 && strcmp(scheme, "https") != 0) {
        fprintf(stderr, "TrapezePDF: only http/https URLs supported\n");
        return -1;
    }

    const char *req_file = "T:trapeze_req";
    const char *raw_file = "T:trapeze_raw";
    if (write_http_request(req_file, host, port, path) != 0) {
        fprintf(stderr, "TrapezePDF: cannot write request file %s\n",
                req_file);
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
        /* Plain HTTP — no openssl; use the `type` + tcp:host/port DOS
         * device? OS4 doesn't have one directly. Fall back to openssl
         * s_client which will happily do plain-text if you pass no
         * -connect over TLS, but for http we'd need a real client.
         * For simplicity in this pass, refuse http:// and require
         * https:// URLs. */
        fprintf(stderr, "TrapezePDF: plain http:// not supported yet, "
                        "use https:// or upgrade the URL manually\n");
        return -1;
    }

    fprintf(stderr, "TrapezePDF: fetching %s...\n", url);
    int rc = system(cmd);
    (void)rc;   /* openssl exits non-zero even on success for some cases */

    unsigned char *body = NULL;
    size_t body_len = 0;
    if (extract_http_body(raw_file, &body, &body_len) != 0) {
        fprintf(stderr, "TrapezePDF: no HTTP response body extracted "
                "(is openssl at %s working?)\n", ossl);
        remove(req_file);
        remove(raw_file);
        return -1;
    }
    remove(req_file);
    remove(raw_file);

    FILE *out = fopen(out_path, "wb");
    if (!out) {
        fprintf(stderr, "TrapezePDF: cannot write %s\n", out_path);
        free(body);
        return -1;
    }
    fwrite(body, 1, body_len, out);
    fclose(out);
    free(body);

    fprintf(stderr, "TrapezePDF: downloaded %zu bytes to %s\n",
            body_len, out_path);
    return 0;
}

int is_url(const char *s)
{
    if (!s || !*s) return 0;
    return (strncmp(s, "http://", 7) == 0 ||
            strncmp(s, "https://", 8) == 0);
}
