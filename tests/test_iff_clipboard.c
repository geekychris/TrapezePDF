/*
 * test_iff_clipboard.c — tests the IFF FTXT/CHRS parsing logic used
 * by TrapezePDF's clipboard reader. clipboard.device itself isn't
 * mockable outside OS4, but the payload parsing is portable.
 *
 * We extract the parsing logic into a tiny helper that we can test
 * with hand-crafted IFF byte streams.
 *
 * Copyright (C) 2026  Chris Collins.  AGPL-3.0.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Same algorithm as in main.c's read_clipboard_text but operating on
 * a caller-provided buffer instead of clipboard.device. Returns
 * malloc'd string or NULL. */
static char *parse_iff_ftxt(const unsigned char *buf, size_t len)
{
    if (!buf || len == 0) return NULL;
    const unsigned char *end = buf + len;
    for (const unsigned char *p = buf; p + 8 <= end; p++) {
        if (p[0] == 'C' && p[1] == 'H' && p[2] == 'R' && p[3] == 'S') {
            unsigned int clen = ((unsigned int)p[4] << 24) |
                                 ((unsigned int)p[5] << 16) |
                                 ((unsigned int)p[6] << 8)  |
                                 ((unsigned int)p[7]);
            if (p + 8 + clen > end) break;
            char *text = (char *)malloc(clen + 1);
            if (!text) return NULL;
            memcpy(text, p + 8, clen);
            text[clen] = '\0';
            while (clen > 0 && (text[clen-1] == '\n' || text[clen-1] == '\r' ||
                                 text[clen-1] == ' ' || text[clen-1] == '\0'))
                text[--clen] = '\0';
            return text;
        }
    }
    /* Fallback: treat whole buffer as text */
    char *text = (char *)malloc(len + 1);
    if (!text) return NULL;
    memcpy(text, buf, len);
    text[len] = '\0';
    size_t l = len;
    while (l > 0 && (text[l-1] == '\n' || text[l-1] == '\r' ||
                      text[l-1] == ' '))
        text[--l] = '\0';
    return text;
}

static int failures = 0;
static int passes = 0;

#define CHECK_STR(actual, expected, desc) do {                        \
    if (!(actual) || strcmp((actual), (expected)) != 0) {             \
        fprintf(stderr, "  FAIL: %s: got \"%s\", expected \"%s\"\n",  \
                (desc), (actual) ? (actual) : "(null)", (expected));  \
        failures++;                                                    \
    } else passes++;                                                   \
} while(0)

static void test_iff_ftxt_with_chrs(void)
{
    printf("iff_ftxt — proper FORM/FTXT/CHRS wrapper\n");
    /* Minimal IFF FTXT: FORM <len> FTXT CHRS <len> <text>
     * FORM<size>FTXTCHRS<size>Hello */
    unsigned char iff[] = {
        'F','O','R','M', 0,0,0,20,
        'F','T','X','T',
        'C','H','R','S', 0,0,0,5, 'H','e','l','l','o'
    };
    char *out = parse_iff_ftxt(iff, sizeof(iff));
    CHECK_STR(out, "Hello", "extract CHRS payload");
    free(out);
}

static void test_iff_ftxt_url(void)
{
    printf("iff_ftxt — URL in clipboard\n");
    const char *url = "https://example.com/foo.pdf";
    size_t ulen = strlen(url);
    unsigned char iff[128] = { 'C','H','R','S',
        (ulen >> 24) & 0xff, (ulen >> 16) & 0xff,
        (ulen >> 8) & 0xff,  ulen & 0xff };
    memcpy(iff + 8, url, ulen);
    char *out = parse_iff_ftxt(iff, 8 + ulen);
    CHECK_STR(out, url, "URL extracted verbatim");
    free(out);
}

static void test_iff_ftxt_trailing_whitespace(void)
{
    printf("iff_ftxt — trims trailing whitespace/newlines\n");
    unsigned char iff[] = {
        'C','H','R','S', 0,0,0,8,
        'h','e','l','l','o','\r','\n',' '
    };
    char *out = parse_iff_ftxt(iff, sizeof(iff));
    CHECK_STR(out, "hello", "trailing whitespace trimmed");
    free(out);
}

static void test_iff_ftxt_no_chrs_fallback(void)
{
    printf("iff_ftxt — plain-text fallback (no CHRS chunk)\n");
    const char *raw = "https://example.com";
    char *out = parse_iff_ftxt((const unsigned char *)raw, strlen(raw));
    CHECK_STR(out, "https://example.com", "raw text passthrough");
    free(out);
}

static void test_iff_ftxt_empty(void)
{
    printf("iff_ftxt — empty input\n");
    char *out = parse_iff_ftxt(NULL, 0);
    if (out != NULL) {
        fprintf(stderr, "  FAIL: NULL input should return NULL\n");
        failures++;
    } else passes++;
    /* Truly empty non-null buffer returns empty string, which is fine */
}

int main(void)
{
    printf("=== TrapezePDF IFF clipboard parsing tests ===\n\n");
    test_iff_ftxt_with_chrs();
    test_iff_ftxt_url();
    test_iff_ftxt_trailing_whitespace();
    test_iff_ftxt_no_chrs_fallback();
    test_iff_ftxt_empty();
    printf("\n=== SUMMARY ===\n");
    printf("  passes:   %d\n", passes);
    printf("  failures: %d\n", failures);
    return failures > 0 ? 1 : 0;
}
