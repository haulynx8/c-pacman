#include "cpm.h"

#include <stdio.h>
#include <assert.h>
#include <string.h>

static int total_tests = 0;
static int passed_tests = 0;

#define TEST(expr, desc) do { \
    total_tests++; \
    if (expr) { \
        printf("  [PASS] %s\n", desc); \
        passed_tests++; \
    } else { \
        fprintf(stderr, "  [FAIL] %s (Line %d)\n", desc, __LINE__); \
    } \
} while(0)

void test_parsing(void) {
    printf("[Suite] SemVer Parsing\n");
    SemVer v;

    TEST(semver_parse("1.2.3", &v) == 0 && v.major == 1 && v.minor == 2 && v.patch == 3, "Parse basic 1.2.3");
    TEST(semver_parse("v1.7.18", &v) == 0 && v.major == 1 && v.minor == 7 && v.patch == 18, "Parse leading 'v' prefix");
    TEST(semver_parse("8.0", &v) == 0 && v.major == 8 && v.minor == 0 && v.patch == 0, "Parse partial 8.0");
    TEST(semver_parse("2.0.0-beta.1", &v) == 0 && v.has_prerelease && strcmp(v.prerelease, "beta.1") == 0, "Parse prerelease -beta.1");
    TEST(semver_parse("1.0.0+build.42", &v) == 0 && v.has_build && strcmp(v.build, "build.42") == 0, "Parse build metadata +build.42");
    TEST(semver_parse("3.0.0-rc1+sha.99", &v) == 0 && v.has_prerelease && v.has_build, "Parse prerelease and build metadata");
}

void test_comparison(void) {
    printf("\n[Suite] SemVer Comparison\n");
    SemVer a, b;

    semver_parse("1.0.0", &a);
    semver_parse("2.0.0", &b);
    TEST(semver_compare(&a, &b) < 0, "1.0.0 < 2.0.0");
    TEST(semver_compare(&b, &a) > 0, "2.0.0 > 1.0.0");

    semver_parse("1.2.3", &a);
    semver_parse("1.2.3", &b);
    TEST(semver_compare(&a, &b) == 0, "1.2.3 == 1.2.3");

    semver_parse("1.2.4", &a);
    semver_parse("1.2.3", &b);
    TEST(semver_compare(&a, &b) > 0, "1.2.4 > 1.2.3");

    // Spec rule: normal version has higher precedence than prerelease
    semver_parse("1.0.0", &a);
    semver_parse("1.0.0-alpha", &b);
    TEST(semver_compare(&a, &b) > 0, "1.0.0 > 1.0.0-alpha");

    // Spec rule: build metadata is ignored in precedence
    semver_parse("1.0.0+build.1", &a);
    semver_parse("1.0.0+build.2", &b);
    TEST(semver_compare(&a, &b) == 0, "Build metadata does not affect precedence");
}

void test_satisfies(void) {
    printf("\n[Suite] SemVer Constraints\n");

    // Caret ranges (Cargo/npm default)
    TEST(semver_satisfies("^8.0", "8.23.0") == 1, "^8.0 satisfies 8.23.0");
    TEST(semver_satisfies("^8.0", "8.0.0") == 1, "^8.0 satisfies 8.0.0");
    TEST(semver_satisfies("^8.0", "9.0.0") == 0, "^8.0 rejects 9.0.0 (breaking change)");
    TEST(semver_satisfies("^8.0", "7.9.0") == 0, "^8.0 rejects 7.9.0");

    // Caret 0.x rule
    TEST(semver_satisfies("^0.2.3", "0.2.5") == 1, "^0.2.3 satisfies 0.2.5");
    TEST(semver_satisfies("^0.2.3", "0.3.0") == 0, "^0.2.3 rejects 0.3.0 (0.x minor is breaking)");

    // Tilde ranges
    TEST(semver_satisfies("~1.2.0", "1.2.5") == 1, "~1.2.0 satisfies 1.2.5");
    TEST(semver_satisfies("~1.2.0", "1.3.0") == 0, "~1.2.0 rejects 1.3.0");

    // Comparison operators
    TEST(semver_satisfies(">= 2.0.0", "2.1.0") == 1, ">= 2.0.0 satisfies 2.1.0");
    TEST(semver_satisfies("< 2.0.0", "2.0.0") == 0, "< 2.0.0 rejects 2.0.0");
    TEST(semver_satisfies("<= 2.0.0", "2.0.0") == 1, "<= 2.0.0 satisfies 2.0.0");

    // Compound range
    TEST(semver_satisfies(">= 1.0.0, < 2.0.0", "1.7.18") == 1, ">= 1.0.0, < 2.0.0 satisfies 1.7.18");
    TEST(semver_satisfies(">= 1.0.0, < 2.0.0", "2.0.1") == 0, ">= 1.0.0, < 2.0.0 rejects 2.0.1");

    // Wildcards
    TEST(semver_satisfies("*", "9.9.9") == 1, "* satisfies any version");
    TEST(semver_satisfies("latest", "1.2.3") == 1, "latest satisfies any version");
}

int main(void) {
    printf("=== CPM SemVer Test Suite ===\n\n");
    test_parsing();
    test_comparison();
    test_satisfies();

    printf("\nSummary: %d/%d tests passed.\n", passed_tests, total_tests);
    return (passed_tests == total_tests) ? 0 : 1;
}
