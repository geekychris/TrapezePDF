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

    printf("\n=== SUMMARY ===\n");
    printf("  passes:   %d\n", passes);
    printf("  failures: %d\n", failures);
    return failures > 0 ? 1 : 0;
}
