#define _POSIX_C_SOURCE 200809L
#include "cpm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Parse a semantic version string like "1.2.3", "v1.7.18", "2.0.0-beta.1+build.123"
int semver_parse(const char *str, SemVer *out) {
    if (!str || !out) return -1;

    memset(out, 0, sizeof(SemVer));

    const char *p = str;
    while (*p && isspace((unsigned char)*p)) p++;

    // Optional leading 'v' or 'V' (e.g. "v1.2.3")
    if (*p == 'v' || *p == 'V') p++;

    if (!isdigit((unsigned char)*p)) return -1;

    // Major
    char *end = NULL;
    out->major = (int)strtol(p, &end, 10);
    if (end == p) return -1;
    p = end;

    // Minor (defaults to 0 if omitted, like "1")
    if (*p == '.') {
        p++;
        if (isdigit((unsigned char)*p)) {
            out->minor = (int)strtol(p, &end, 10);
            p = end;
        }
    }

    // Patch (defaults to 0 if omitted, like "1.2")
    if (*p == '.') {
        p++;
        if (isdigit((unsigned char)*p)) {
            out->patch = (int)strtol(p, &end, 10);
            p = end;
        }
    }

    // Prerelease (e.g. "-alpha.1", "-rc2")
    if (*p == '-') {
        p++;
        out->has_prerelease = 1;
        int idx = 0;
        while (*p && *p != '+' && !isspace((unsigned char)*p) && idx < (int)sizeof(out->prerelease) - 1) {
            out->prerelease[idx++] = *p++;
        }
        out->prerelease[idx] = '\0';
    }

    // Build metadata (e.g. "+20240101")
    if (*p == '+') {
        p++;
        out->has_build = 1;
        int idx = 0;
        while (*p && !isspace((unsigned char)*p) && idx < (int)sizeof(out->build) - 1) {
            out->build[idx++] = *p++;
        }
        out->build[idx] = '\0';
    }

    return 0;
}

// Format back to string
void semver_to_string(const SemVer *v, char *buf, size_t sz) {
    if (!v || !buf || sz == 0) return;

    if (v->has_prerelease && v->has_build) {
        snprintf(buf, sz, "%d.%d.%d-%s+%s", v->major, v->minor, v->patch, v->prerelease, v->build);
    } else if (v->has_prerelease) {
        snprintf(buf, sz, "%d.%d.%d-%s", v->major, v->minor, v->patch, v->prerelease);
    } else if (v->has_build) {
        snprintf(buf, sz, "%d.%d.%d+%s", v->major, v->minor, v->patch, v->build);
    } else {
        snprintf(buf, sz, "%d.%d.%d", v->major, v->minor, v->patch);
    }
}

// Compare two SemVer structs according to SemVer 2.0.0 rules:
// Returns: -1 if a < b, 0 if a == b, 1 if a > b
int semver_compare(const SemVer *a, const SemVer *b) {
    if (a->major != b->major) return (a->major > b->major) ? 1 : -1;
    if (a->minor != b->minor) return (a->minor > b->minor) ? 1 : -1;
    if (a->patch != b->patch) return (a->patch > b->patch) ? 1 : -1;

    // Normal version has higher precedence than a prerelease version:
    // 1.0.0 > 1.0.0-alpha
    if (!a->has_prerelease && b->has_prerelease) return 1;
    if (a->has_prerelease && !b->has_prerelease) return -1;

    // Both have prerelease: lexical comparison
    if (a->has_prerelease && b->has_prerelease) {
        int cmp = strcmp(a->prerelease, b->prerelease);
        if (cmp != 0) return (cmp > 0) ? 1 : -1;
    }

    // Build metadata is ignored when determining version precedence
    return 0;
}

// Helper to evaluate a single clause, e.g. "^1.2.3", ">=2.0.0", "<3.0.0", "=1.0.0"
static int satisfies_clause(const char *clause, const SemVer *v) {
    char copy[128];
    strncpy(copy, clause, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';
    trim_whitespace(copy);

    if (strcmp(copy, "*") == 0 || strcmp(copy, "latest") == 0 || copy[0] == '\0') {
        return 1;
    }

    const char *p = copy;
    enum Op { OP_CARET, OP_TILDE, OP_GTE, OP_LTE, OP_GT, OP_LT, OP_EQ } op = OP_EQ;

    if (p[0] == '^') {
        op = OP_CARET;
        p++;
    } else if (p[0] == '~') {
        op = OP_TILDE;
        p++;
    } else if (p[0] == '>' && p[1] == '=') {
        op = OP_GTE;
        p += 2;
    } else if (p[0] == '<' && p[1] == '=') {
        op = OP_LTE;
        p += 2;
    } else if (p[0] == '>') {
        op = OP_GT;
        p++;
    } else if (p[0] == '<') {
        op = OP_LT;
        p++;
    } else if (p[0] == '=') {
        op = OP_EQ;
        p++;
    }

    while (*p && isspace((unsigned char)*p)) p++;

    SemVer req;
    if (semver_parse(p, &req) != 0) {
        return 0; // Invalid requirement version
    }

    int cmp = semver_compare(v, &req);

    switch (op) {
        case OP_EQ:
            return (cmp == 0);
        case OP_GT:
            return (cmp > 0);
        case OP_GTE:
            return (cmp >= 0);
        case OP_LT:
            return (cmp < 0);
        case OP_LTE:
            return (cmp <= 0);
        case OP_CARET:
            // Caret range (^1.2.3): allows changes that do not modify the left-most non-zero digit
            if (cmp < 0) return 0;
            if (req.major > 0) {
                return (v->major == req.major);
            } else if (req.minor > 0) {
                return (v->major == 0 && v->minor == req.minor);
            } else {
                return (v->major == 0 && v->minor == 0 && v->patch == req.patch);
            }
        case OP_TILDE:
            // Tilde range (~1.2.3): allows patch-level changes if minor is specified
            if (cmp < 0) return 0;
            return (v->major == req.major && v->minor == req.minor);
    }

    return 0;
}

// Check if version_str satisfies a full constraint expression (e.g. "^8.0", ">= 1.0.0, < 2.0.0")
int semver_satisfies(const char *constraint, const char *version_str) {
    if (!constraint || !version_str) return 0;

    SemVer v;
    if (semver_parse(version_str, &v) != 0) {
        return 0; // Cannot parse candidate version
    }

    char buf[256];
    strncpy(buf, constraint, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    trim_whitespace(buf);

    if (strcmp(buf, "*") == 0 || strcmp(buf, "latest") == 0 || buf[0] == '\0') {
        return 1;
    }

    // Split on comma ',' for compound constraints like ">= 1.0.0, < 2.0.0"
    char *saveptr = NULL;
    char *token = strtok_r(buf, ",", &saveptr);
    while (token) {
        if (!satisfies_clause(token, &v)) {
            return 0; // One requirement failed
        }
        token = strtok_r(NULL, ",", &saveptr);
    }

    return 1;
}
