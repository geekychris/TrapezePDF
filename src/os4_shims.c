/*
 * os4_shims.c — POSIX/BSD functions newlib on OS4 doesn't provide but
 * MuPDF (or our own code) references. Kept in a separate file so any
 * future additions are easy to find.
 *
 * License: AGPL-3.0 (matches the rest of pdfview-os4).
 */
#include <time.h>
#include <stdlib.h>
#include <string.h>

/* --- memmem ---------------------------------------------------------
 * GNU extension; newlib doesn't ship it. Naïve O(nh*ne) fine for our
 * needle sizes (small — HTTP headers scanning). */
void *memmem(const void *haystack, size_t haystack_len,
              const void *needle, size_t needle_len)
{
    if (needle_len == 0) return (void *)haystack;
    if (haystack_len < needle_len) return NULL;
    const unsigned char *h = (const unsigned char *)haystack;
    const unsigned char *n = (const unsigned char *)needle;
    size_t limit = haystack_len - needle_len;
    for (size_t i = 0; i <= limit; i++) {
        if (memcmp(h + i, n, needle_len) == 0)
            return (void *)(h + i);
    }
    return NULL;
}

/* --- timegm ---------------------------------------------------------
 * BSD extension; converts a `struct tm` interpreted as UTC into a
 * `time_t`. MuPDF's pdf-parse.c uses it for /CreationDate parsing.
 *
 * Portable implementation using the TZ hack: temporarily set the
 * timezone to UTC, call mktime (which interprets tm as local), then
 * restore. Safe on OS4 because we do it single-threaded during
 * document parsing. */
time_t timegm(struct tm *tm)
{
    /* newlib on OS4 doesn't ship unsetenv, so just save/restore via
     * setenv. Setting to empty string is treated as "unset" by tzset
     * on most libcs and defaults to UTC — good enough here. */
    char *saved_tz = getenv("TZ");
    setenv("TZ", "UTC0", 1);
    tzset();
    time_t t = mktime(tm);
    setenv("TZ", saved_tz ? saved_tz : "", 1);
    tzset();
    return t;
}
