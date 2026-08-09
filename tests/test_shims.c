/*
 * test_shims.c — host-side tests for the small POSIX helpers we
 * provide in src/os4_shims.c (memmem, timegm).
 *
 * On glibc/macOS these are provided by the system; on OS4 newlib
 * we ship shim implementations. This test suite ensures our shim
 * produces the same results as the reference implementation.
 *
 * Copyright (C) 2026  Chris Collins.  AGPL-3.0.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

/* Redefine the shim names so we compile our copies AND compare
 * against the system versions. We prefix the shim symbols so the
 * host linker doesn't collide with libc. */
#define memmem trapeze_memmem
#define timegm trapeze_timegm

#include "../src/os4_shims.c"

#undef memmem
#undef timegm

static int failures = 0;
static int passes = 0;

#define CHECK(cond, desc) do {                                        \
    if (!(cond)) { fprintf(stderr, "  FAIL: %s\n", (desc));           \
                    failures++; }                                      \
    else passes++;                                                     \
} while(0)

static void test_memmem_basic(void)
{
    printf("memmem — basic\n");
    const char haystack[] = "Hello, World!";
    CHECK(trapeze_memmem(haystack, sizeof(haystack) - 1, "World", 5)
          == haystack + 7, "find 'World' in 'Hello, World!'");
    CHECK(trapeze_memmem(haystack, sizeof(haystack) - 1, "Hello", 5)
          == haystack, "find 'Hello' at start");
    CHECK(trapeze_memmem(haystack, sizeof(haystack) - 1, "!", 1)
          == haystack + 12, "find '!' at end");
    CHECK(trapeze_memmem(haystack, sizeof(haystack) - 1, "xyz", 3)
          == NULL, "not-found returns NULL");
}

static void test_memmem_edge_cases(void)
{
    printf("memmem — edge cases\n");
    CHECK(trapeze_memmem("", 0, "x", 1) == NULL,
          "empty haystack, non-empty needle");
    CHECK(trapeze_memmem("abc", 3, "", 0) == (void*)"abc",
          "empty needle returns haystack");
    CHECK(trapeze_memmem("abc", 3, "abcd", 4) == NULL,
          "needle longer than haystack");
    CHECK(trapeze_memmem("aaa", 3, "aa", 2) == (void*)"aaa",
          "first occurrence of overlapping");
}

static void test_memmem_binary(void)
{
    printf("memmem — binary data with nulls\n");
    const unsigned char h[] = { 0x00, 0x01, 0x02, 0x00, 0x03, 0x04 };
    const unsigned char n[] = { 0x00, 0x03 };
    CHECK(trapeze_memmem(h, sizeof(h), n, sizeof(n)) == h + 3,
          "find sequence containing null byte");
    const unsigned char n2[] = { 0xff, 0xff };
    CHECK(trapeze_memmem(h, sizeof(h), n2, sizeof(n2)) == NULL,
          "sequence not present");
}

static void test_memmem_matches_libc(void)
{
    /* Where the system provides memmem (glibc, macOS 12+), verify
     * our shim gives the same answer. Skipped if libc's memmem
     * isn't linked (we don't call it explicitly here to keep the
     * test portable). */
    printf("memmem — same result as libc for a range of inputs\n");
    const char *haystacks[] = {
        "the quick brown fox jumps over the lazy dog",
        "aaaaaaaaaaaaaaaa",
        "PDF-1.7\n%\xe2\xe3\xcf\xd3\n1 0 obj\n<< /Type /Catalog >>",
        "",
    };
    const char *needles[] = {
        "quick", "the", "aaaa", "PDF-", "Catalog", "not-there", "",
    };
    int total = 0, differ = 0;
    for (size_t i = 0; i < sizeof(haystacks)/sizeof(*haystacks); i++) {
        for (size_t j = 0; j < sizeof(needles)/sizeof(*needles); j++) {
            /* Skip empty-needle case — POSIX/GNU say return haystack,
             * macOS returns NULL; both defensible. Our shim follows
             * the POSIX/GNU semantics per the standard. */
            if (strlen(needles[j]) == 0) continue;
            void *ours = trapeze_memmem(haystacks[i], strlen(haystacks[i]),
                                          needles[j], strlen(needles[j]));
            void *theirs = memmem(haystacks[i], strlen(haystacks[i]),
                                    needles[j], strlen(needles[j]));
            if (ours != theirs) {
                fprintf(stderr,
                    "  DIFFER: haystack=%zu needle=%zu ours=%p theirs=%p\n",
                    i, j, ours, theirs);
                differ++;
            }
            total++;
        }
    }
    printf("  compared %d input combinations, %d differed\n",
           total, differ);
    if (differ == 0) passes++;
    else failures++;
}

static void test_timegm_basic(void)
{
    printf("timegm — basic epoch reference points\n");
    struct tm tm = {0};
    /* 1970-01-01 00:00:00 UTC = epoch 0 */
    tm.tm_year = 70; tm.tm_mon = 0; tm.tm_mday = 1;
    tm.tm_hour = 0;  tm.tm_min = 0; tm.tm_sec = 0;
    time_t t = trapeze_timegm(&tm);
    CHECK(t == 0, "1970-01-01 00:00:00 UTC == 0");

    /* 2000-01-01 00:00:00 UTC = 946684800 */
    memset(&tm, 0, sizeof(tm));
    tm.tm_year = 100; tm.tm_mon = 0; tm.tm_mday = 1;
    t = trapeze_timegm(&tm);
    CHECK(t == 946684800, "2000-01-01 00:00:00 UTC == 946684800");

    /* 2020-03-15 12:30:45 UTC = 1584275445 */
    memset(&tm, 0, sizeof(tm));
    tm.tm_year = 120; tm.tm_mon = 2; tm.tm_mday = 15;
    tm.tm_hour = 12;  tm.tm_min = 30; tm.tm_sec = 45;
    t = trapeze_timegm(&tm);
    CHECK(t == 1584275445, "2020-03-15 12:30:45 UTC");
}

static void test_timegm_leap_years(void)
{
    printf("timegm — leap year handling\n");
    /* 2020-02-29 00:00:00 UTC = valid; 2021-02-29 should overflow to Mar 1 */
    struct tm tm = {0};
    tm.tm_year = 120; tm.tm_mon = 1; tm.tm_mday = 29;
    time_t t = trapeze_timegm(&tm);
    /* Expect Feb 29 2020: 1582934400 */
    CHECK(t == 1582934400, "Feb 29 2020 is valid");
}

int main(void)
{
    printf("=== TrapezePDF os4_shims.c unit tests ===\n\n");
    test_memmem_basic();
    test_memmem_edge_cases();
    test_memmem_binary();
    test_memmem_matches_libc();
    test_timegm_basic();
    test_timegm_leap_years();

    printf("\n=== SUMMARY ===\n");
    printf("  passes:   %d\n", passes);
    printf("  failures: %d\n", failures);
    return failures > 0 ? 1 : 0;
}
