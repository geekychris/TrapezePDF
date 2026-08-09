/*
 * test_net.c — host-side unit tests for TrapezePDF's URL parsing +
 * is_url helpers. Compiles against the same net.c source used for
 * the OS4 target (native compiler, not cross), and exercises the
 * portable code paths.
 *
 * Build:  ./tests/run.sh   (or cc -Isrc tests/test_net.c src/net.c -o t)
 *
 * Copyright (C) 2026  Chris Collins.  AGPL-3.0.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../src/net.h"

/* Expose internal helpers so we can regression-test the Host: header
 * and body-parsing bugs that were fixed once tracemonkey.pdf started
 * downloading as 1016315 bytes exactly (matching the reference). */
extern int trapeze_write_http_request(const char *req_file,
                                       const char *host, int port,
                                       const char *path);
extern int trapeze_extract_http_body(const char *raw_file,
                                      unsigned char **body_out,
                                      size_t *body_len_out);

static int failures = 0;
static int passes = 0;

#define EXPECT_EQ_INT(actual, expected, desc) do {                    \
    if ((actual) != (expected)) {                                     \
        fprintf(stderr, "  FAIL: %s: got %d, expected %d\n",          \
                (desc), (int)(actual), (int)(expected));              \
        failures++;                                                    \
    } else {                                                           \
        passes++;                                                      \
    }                                                                  \
} while(0)

#define EXPECT_EQ_STR(actual, expected, desc) do {                    \
    if (strcmp((actual), (expected)) != 0) {                          \
        fprintf(stderr, "  FAIL: %s: got \"%s\", expected \"%s\"\n",  \
                (desc), (actual), (expected));                        \
        failures++;                                                    \
    } else {                                                           \
        passes++;                                                      \
    }                                                                  \
} while(0)

#define EXPECT_TRUE(cond, desc) do {                                   \
    if (!(cond)) {                                                     \
        fprintf(stderr, "  FAIL: %s: expected truthy\n", (desc));     \
        failures++;                                                    \
    } else {                                                           \
        passes++;                                                      \
    }                                                                  \
} while(0)

/* --- is_url tests --------------------------------------------------- */
static void test_is_url_positives(void)
{
    printf("is_url — positives\n");
    EXPECT_TRUE(is_url("http://example.com/foo.pdf"),
                "http://example.com");
    EXPECT_TRUE(is_url("https://example.com/"),
                "https:// bare");
    EXPECT_TRUE(is_url("https://a.b.c.d/x?y=z"),
                "https:// query");
    EXPECT_TRUE(is_url("http://192.168.1.1:8080/doc"),
                "http:// with port + ip");
    EXPECT_TRUE(is_url("https://example.com:443/path/to/file.pdf"),
                "https:// with explicit 443");
}

static void test_is_url_negatives(void)
{
    printf("is_url — negatives\n");
    EXPECT_TRUE(!is_url(""),                    "empty string");
    EXPECT_TRUE(!is_url(NULL),                  "NULL");
    EXPECT_TRUE(!is_url("DH1:test.pdf"),        "Amiga volume path");
    EXPECT_TRUE(!is_url("/tmp/foo.pdf"),        "Unix absolute path");
    EXPECT_TRUE(!is_url("ftp://ftp.example.com/x"), "ftp:// (not supported)");
    EXPECT_TRUE(!is_url("file:///etc/passwd"),  "file:// (not supported)");
    EXPECT_TRUE(!is_url("HTTPS://example.com/"), "uppercase HTTPS (strict prefix)");
    EXPECT_TRUE(!is_url("mail:foo@bar"),         "mail:");
    EXPECT_TRUE(!is_url("http"),                 "just 'http' no ://");
    EXPECT_TRUE(!is_url("https"),                "just 'https' no ://");
}

/* --- trapeze_parse_url tests --------------------------------------- */
static void test_parse_simple_https(void)
{
    printf("parse_url — simple https\n");
    char scheme[16], host[256], path[512];
    int port;
    int rc = trapeze_parse_url("https://example.com/",
        scheme, sizeof(scheme), host, sizeof(host), &port,
        path, sizeof(path));
    EXPECT_EQ_INT(rc, 0,                     "return value");
    EXPECT_EQ_STR(scheme, "https",           "scheme");
    EXPECT_EQ_STR(host,   "example.com",     "host");
    EXPECT_EQ_INT(port,   443,               "default https port");
    EXPECT_EQ_STR(path,   "/",               "path");
}

static void test_parse_simple_http(void)
{
    printf("parse_url — simple http\n");
    char scheme[16], host[256], path[512];
    int port;
    int rc = trapeze_parse_url("http://example.com/",
        scheme, sizeof(scheme), host, sizeof(host), &port,
        path, sizeof(path));
    EXPECT_EQ_INT(rc, 0,                     "return value");
    EXPECT_EQ_STR(scheme, "http",            "scheme");
    EXPECT_EQ_INT(port,   80,                "default http port");
}

static void test_parse_explicit_port(void)
{
    printf("parse_url — explicit port\n");
    char scheme[16], host[256], path[512];
    int port;
    int rc = trapeze_parse_url("https://example.com:8443/foo",
        scheme, sizeof(scheme), host, sizeof(host), &port,
        path, sizeof(path));
    EXPECT_EQ_INT(rc, 0,                     "return value");
    EXPECT_EQ_STR(host,   "example.com",     "host without port");
    EXPECT_EQ_INT(port,   8443,              "explicit port");
    EXPECT_EQ_STR(path,   "/foo",            "path");
}

static void test_parse_path_with_query(void)
{
    printf("parse_url — path with query\n");
    char scheme[16], host[256], path[512];
    int port;
    int rc = trapeze_parse_url("https://example.com/api/v1/doc?id=42&fmt=pdf",
        scheme, sizeof(scheme), host, sizeof(host), &port,
        path, sizeof(path));
    EXPECT_EQ_INT(rc, 0,                     "return value");
    EXPECT_EQ_STR(path,   "/api/v1/doc?id=42&fmt=pdf",
                                              "path with query preserved");
}

static void test_parse_no_path(void)
{
    printf("parse_url — no path\n");
    char scheme[16], host[256], path[512];
    int port;
    int rc = trapeze_parse_url("https://example.com",
        scheme, sizeof(scheme), host, sizeof(host), &port,
        path, sizeof(path));
    EXPECT_EQ_INT(rc, 0,                     "return value");
    EXPECT_EQ_STR(host,   "example.com",     "host");
    EXPECT_EQ_STR(path,   "/",               "default path when omitted");
}

static void test_parse_ip_address(void)
{
    printf("parse_url — IPv4 address\n");
    char scheme[16], host[256], path[512];
    int port;
    int rc = trapeze_parse_url("http://192.168.1.100:8080/file.pdf",
        scheme, sizeof(scheme), host, sizeof(host), &port,
        path, sizeof(path));
    EXPECT_EQ_INT(rc, 0,                     "return value");
    EXPECT_EQ_STR(host,   "192.168.1.100",   "IPv4 host");
    EXPECT_EQ_INT(port,   8080,              "port");
}

static void test_parse_case_normalization(void)
{
    printf("parse_url — scheme case\n");
    char scheme[16], host[256], path[512];
    int port;
    int rc = trapeze_parse_url("HTTPS://Example.COM/Foo",
        scheme, sizeof(scheme), host, sizeof(host), &port,
        path, sizeof(path));
    EXPECT_EQ_INT(rc, 0,                     "return value");
    EXPECT_EQ_STR(scheme, "https",           "scheme lowercased");
    /* Host preserved as-given — not our job to case-normalize hosts */
    EXPECT_EQ_STR(host,   "Example.COM",     "host preserved");
    EXPECT_EQ_STR(path,   "/Foo",            "path preserved");
}

static void test_parse_invalid_urls(void)
{
    printf("parse_url — invalid URLs return -1\n");
    char scheme[16], host[256], path[512];
    int port;
    /* No scheme separator */
    EXPECT_EQ_INT(trapeze_parse_url("example.com/foo",
        scheme, sizeof(scheme), host, sizeof(host), &port,
        path, sizeof(path)), -1, "no ://");
    /* Empty host */
    EXPECT_EQ_INT(trapeze_parse_url("https:///path",
        scheme, sizeof(scheme), host, sizeof(host), &port,
        path, sizeof(path)), -1, "empty host");
    /* Port out of range */
    EXPECT_EQ_INT(trapeze_parse_url("https://host:99999/x",
        scheme, sizeof(scheme), host, sizeof(host), &port,
        path, sizeof(path)), -1, "port too high");
}

static void test_parse_buffer_overflow_protection(void)
{
    printf("parse_url — buffer overflow protection\n");
    char tiny_scheme[3], host[256], path[512];
    int port;
    /* scheme "https" needs 6 bytes; tiny_scheme is 3 → should fail */
    int rc = trapeze_parse_url("https://example.com/",
        tiny_scheme, sizeof(tiny_scheme), host, sizeof(host), &port,
        path, sizeof(path));
    EXPECT_EQ_INT(rc, -1, "reject scheme buffer overflow");

    char scheme[16], tiny_host[4], path2[512];
    rc = trapeze_parse_url("https://example.com/",
        scheme, sizeof(scheme), tiny_host, sizeof(tiny_host), &port,
        path2, sizeof(path2));
    EXPECT_EQ_INT(rc, -1, "reject host buffer overflow");
}

/* --- HTTP request generation regressions --------------------------- */

/* Read a file into a NUL-terminated malloc'd buffer. Small enough for
 * tests. Returns NULL if the file doesn't exist. */
static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf); fclose(f); return NULL;
    }
    buf[n] = '\0';
    fclose(f);
    return buf;
}

/* Regression: earlier version wrote `Host: raw.githubusercontent.com0`
 * for port 443 (a `%d` that fired even for standard ports). GitHub's
 * shared-IP hosting returned "Site not found" HTML instead of the PDF.
 * The Host header must be exactly the hostname with no trailing "0"
 * for standard ports, and hostname:port otherwise. */
static void test_http_request_host_header_std_port(void)
{
    printf("http_request — Host header for port 443\n");
    const char *tmp = "/tmp/trapeze_test_req_443";
    remove(tmp);
    int rc = trapeze_write_http_request(tmp, "raw.githubusercontent.com",
                                          443, "/x.pdf");
    EXPECT_EQ_INT(rc, 0, "write ok");
    char *body = slurp(tmp);
    EXPECT_TRUE(body != NULL, "req file exists");
    if (body) {
        EXPECT_TRUE(strstr(body, "\r\nHost: raw.githubusercontent.com\r\n") != NULL,
                    "Host header exact match (no trailing digit)");
        EXPECT_TRUE(strstr(body, "raw.githubusercontent.com0") == NULL,
                    "no stray '0' anywhere");
        EXPECT_TRUE(strstr(body, "GET /x.pdf HTTP/1.0\r\n") != NULL,
                    "request line ok");
        free(body);
    }
    remove(tmp);
}

static void test_http_request_host_header_port_80(void)
{
    printf("http_request — Host header for port 80\n");
    const char *tmp = "/tmp/trapeze_test_req_80";
    remove(tmp);
    trapeze_write_http_request(tmp, "example.com", 80, "/");
    char *body = slurp(tmp);
    EXPECT_TRUE(body != NULL, "req file");
    if (body) {
        EXPECT_TRUE(strstr(body, "\r\nHost: example.com\r\n") != NULL,
                    "Host header bare for port 80");
        EXPECT_TRUE(strstr(body, "example.com0") == NULL, "no stray '0'");
        free(body);
    }
    remove(tmp);
}

static void test_http_request_host_header_custom_port(void)
{
    printf("http_request — Host header for custom port\n");
    const char *tmp = "/tmp/trapeze_test_req_8443";
    remove(tmp);
    trapeze_write_http_request(tmp, "example.com", 8443, "/api");
    char *body = slurp(tmp);
    EXPECT_TRUE(body != NULL, "req file");
    if (body) {
        EXPECT_TRUE(strstr(body, "\r\nHost: example.com:8443\r\n") != NULL,
                    "Host: host:port for non-standard port");
        free(body);
    }
    remove(tmp);
}

/* --- HTTP body extraction regressions ------------------------------ */

/* Helper: write raw bytes to a tmp file, return path (caller must remove). */
static void write_raw(const char *path, const unsigned char *bytes, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (f) { fwrite(bytes, 1, n, f); fclose(f); }
}

/* Regression: earlier code did `sep[1] == '\n' ? 2 : 4` — but sep[1]
 * is '\n' for BOTH `\r\n\r\n` and `\n\n`, so we always advanced 2
 * bytes and left a stray `\r\n` at the start of the body. */
static void test_extract_body_strips_full_crlfcrlf(void)
{
    printf("extract_body — strips full \\r\\n\\r\\n cleanly\n");
    const char *tmp = "/tmp/trapeze_test_raw_crlfcrlf";
    const char *resp =
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: application/pdf\r\n"
        "\r\n"
        "PDFBODYSTART";
    write_raw(tmp, (const unsigned char *)resp, strlen(resp));
    unsigned char *body = NULL; size_t body_len = 0;
    int rc = trapeze_extract_http_body(tmp, &body, &body_len);
    EXPECT_EQ_INT(rc, 0, "return ok");
    EXPECT_EQ_INT((int)body_len, 12, "body length = 'PDFBODYSTART'");
    if (body_len == 12) {
        EXPECT_TRUE(memcmp(body, "PDFBODYSTART", 12) == 0,
                    "body starts exactly at 'P' (no leading \\r\\n)");
    }
    free(body);
    remove(tmp);
}

static void test_extract_body_strips_lflf(void)
{
    printf("extract_body — strips bare \\n\\n\n");
    const char *tmp = "/tmp/trapeze_test_raw_lflf";
    const char *resp =
        "HTTP/1.0 200 OK\n"
        "Content-Type: text/plain\n"
        "\n"
        "BODY";
    write_raw(tmp, (const unsigned char *)resp, strlen(resp));
    unsigned char *body = NULL; size_t body_len = 0;
    int rc = trapeze_extract_http_body(tmp, &body, &body_len);
    EXPECT_EQ_INT(rc, 0, "return ok");
    EXPECT_EQ_INT((int)body_len, 4, "body length = 4");
    if (body_len == 4) {
        EXPECT_TRUE(memcmp(body, "BODY", 4) == 0, "body content");
    }
    free(body);
    remove(tmp);
}

/* Regression: openssl s_client on OS4 appends its TLS session-info
 * output to captured stdout, so the raw response has PDF bytes then
 * cert-chain "depth=… verify return:1" trailing text. Content-Length
 * lets us bound the body precisely. */
static void test_extract_body_honors_content_length(void)
{
    printf("extract_body — Content-Length bounds body\n");
    const char *tmp = "/tmp/trapeze_test_raw_cl";
    /* 5-byte body "HELLO" followed by junk that would otherwise
     * be included. */
    const char *resp =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 5\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n"
        "HELLOconnect=OK depth=3 CN=whatever\n";
    write_raw(tmp, (const unsigned char *)resp, strlen(resp));
    unsigned char *body = NULL; size_t body_len = 0;
    int rc = trapeze_extract_http_body(tmp, &body, &body_len);
    EXPECT_EQ_INT(rc, 0, "return ok");
    EXPECT_EQ_INT((int)body_len, 5, "body clipped to Content-Length");
    if (body_len == 5) {
        EXPECT_TRUE(memcmp(body, "HELLO", 5) == 0,
                    "body is exactly 'HELLO', trailing junk dropped");
    }
    free(body);
    remove(tmp);
}

static void test_extract_body_content_length_case_insensitive(void)
{
    printf("extract_body — Content-Length header lookup is case-insensitive\n");
    const char *tmp = "/tmp/trapeze_test_raw_cl_lc";
    const char *resp =
        "HTTP/1.1 200 OK\r\n"
        "content-length: 3\r\n"      /* lowercase, as some servers send */
        "\r\n"
        "ABCextra";
    write_raw(tmp, (const unsigned char *)resp, strlen(resp));
    unsigned char *body = NULL; size_t body_len = 0;
    trapeze_extract_http_body(tmp, &body, &body_len);
    EXPECT_EQ_INT((int)body_len, 3, "lowercase content-length honored");
    if (body_len == 3) {
        EXPECT_TRUE(memcmp(body, "ABC", 3) == 0, "body content");
    }
    free(body);
    remove(tmp);
}

int main(void)
{
    printf("=== TrapezePDF net.c unit tests ===\n\n");
    test_is_url_positives();
    test_is_url_negatives();
    test_parse_simple_https();
    test_parse_simple_http();
    test_parse_explicit_port();
    test_parse_path_with_query();
    test_parse_no_path();
    test_parse_ip_address();
    test_parse_case_normalization();
    test_parse_invalid_urls();
    test_parse_buffer_overflow_protection();
    test_http_request_host_header_std_port();
    test_http_request_host_header_port_80();
    test_http_request_host_header_custom_port();
    test_extract_body_strips_full_crlfcrlf();
    test_extract_body_strips_lflf();
    test_extract_body_honors_content_length();
    test_extract_body_content_length_case_insensitive();

    printf("\n=== SUMMARY ===\n");
    printf("  passes:   %d\n", passes);
    printf("  failures: %d\n", failures);
    return failures > 0 ? 1 : 0;
}
