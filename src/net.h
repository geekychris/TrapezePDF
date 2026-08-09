/*
 * net.h — URL fetching for TrapezePDF.
 * Copyright (C) 2026  Chris Collins.  AGPL-3.0 (see LICENSE).
 */
#ifndef TRAPEZE_NET_H
#define TRAPEZE_NET_H

#include <stddef.h>   /* size_t */

/* Return 1 if s starts with http:// or https://. */
int is_url(const char *s);

/* Fetch url to out_path via `openssl s_client` shell-out. Returns 0
 * on success, non-zero on failure (logs error to stderr).
 * Requires an `openssl` binary at $OPENSSL_PATH or DH1:openssl. */
int fetch_url(const char *url, const char *out_path);

/* Exposed for unit testing (not for general callers). Fills scheme,
 * host, port, path from url. Returns 0 on success, -1 on parse fail.
 * Only http/https schemes recognized. */
int trapeze_parse_url(const char *url,
                       char *scheme, size_t scheme_sz,
                       char *host, size_t host_sz,
                       int *port,
                       char *path, size_t path_sz);

#endif
