#include "cpm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Known CVE database for popular C libraries
static const Vulnerability VULN_DB[] = {
    {
        .package_name = "cJSON",
        .affected_constraint = "< 1.7.14",
        .cve_id = "CVE-2020-24978",
        .severity = "HIGH",
        .description = "Out-of-bounds read in cJSON_PrintBuffered"
    },
    {
        .package_name = "cJSON",
        .affected_constraint = "< 1.7.15",
        .cve_id = "CVE-2021-30461",
        .severity = "MEDIUM",
        .description = "Null pointer dereference via crafted input"
    },
    {
        .package_name = "libcurl",
        .affected_constraint = "< 8.4.0",
        .cve_id = "CVE-2023-38545",
        .severity = "HIGH",
        .description = "SOCKS5 heap-based buffer overflow during handshake"
    },
    {
        .package_name = "openssl",
        .affected_constraint = ">= 3.0.0, < 3.0.7",
        .cve_id = "CVE-2022-3602",
        .severity = "HIGH",
        .description = "X.509 email address buffer overflow during certificate verification"
    },
    {
        .package_name = "zlib",
        .affected_constraint = "< 1.2.12",
        .cve_id = "CVE-2018-25032",
        .severity = "HIGH",
        .description = "Out-of-bounds write in deflate when copying window"
    },
    {
        .package_name = "sqlite3",
        .affected_constraint = "< 3.39.2",
        .cve_id = "CVE-2022-35737",
        .severity = "HIGH",
        .description = "Array-bounds overflow in sqlite3_str_vappendf"
    }
};

static const int VULN_COUNT = sizeof(VULN_DB) / sizeof(VULN_DB[0]);

static void export_sbom(const Lockfile *lock, const ProjectConfig *config) {
    FILE *fp = fopen("cpm.sbom.json", "w");
    if (!fp) {
        perror("fopen cpm.sbom.json");
        return;
    }

    fprintf(fp, "{\n");
    fprintf(fp, "  \"$schema\": \"https://cyclonedx.org/schema/bom-1.5.json\",\n");
    fprintf(fp, "  \"bomFormat\": \"CycloneDX\",\n");
    fprintf(fp, "  \"specVersion\": \"1.5\",\n");
    fprintf(fp, "  \"metadata\": {\n");
    fprintf(fp, "    \"component\": {\n");
    fprintf(fp, "      \"name\": \"%s\",\n", config->name[0] ? config->name : "project");
    fprintf(fp, "      \"version\": \"%s\",\n", config->version[0] ? config->version : "0.1.0");
    fprintf(fp, "      \"type\": \"application\"\n");
    fprintf(fp, "    },\n");
    fprintf(fp, "    \"tools\": [\n");
    fprintf(fp, "      { \"vendor\": \"cpm\", \"name\": \"cpm\", \"version\": \"%s\" }\n", CPM_VERSION);
    fprintf(fp, "    ]\n");
    fprintf(fp, "  },\n");
    fprintf(fp, "  \"components\": [\n");

    for (int i = 0; i < lock->package_count; i++) {
        const LockedPackage *p = &lock->packages[i];
        const char *ver = p->version[0] ? p->version : (p->tag[0] ? p->tag : "unknown");

        fprintf(fp, "    {\n");
        fprintf(fp, "      \"type\": \"library\",\n");
        fprintf(fp, "      \"name\": \"%s\",\n", p->name);
        fprintf(fp, "      \"version\": \"%s\",\n", ver);

        if (p->type == DEP_TYPE_GIT) {
            fprintf(fp, "      \"purl\": \"pkg:github/%s@%s\",\n", p->name, ver);
            if (p->commit[0]) {
                fprintf(fp, "      \"hashes\": [ { \"alg\": \"GIT-COMMIT\", \"content\": \"%s\" } ]\n", p->commit);
            }
        } else if (p->type == DEP_TYPE_URL) {
            fprintf(fp, "      \"purl\": \"pkg:generic/%s@%s\",\n", p->name, ver);
            if (p->checksum[0]) {
                fprintf(fp, "      \"hashes\": [ { \"alg\": \"SHA-256\", \"content\": \"%s\" } ]\n", p->checksum);
            }
        } else {
            fprintf(fp, "      \"purl\": \"pkg:generic/%s@%s\"\n", p->name, ver);
        }

        fprintf(fp, "    }%s\n", (i == lock->package_count - 1) ? "" : ",");
    }

    fprintf(fp, "  ]\n");
    fprintf(fp, "}\n");

    fclose(fp);
    printf("[cpm] Exported CycloneDX SBOM to: cpm.sbom.json\n");
}

int cmd_audit(int generate_sbom) {
    Lockfile lock;
    if (lockfile_read("cpm.lock", &lock) != 0) {
        printf("[cpm] No cpm.lock found. Running 'cpm install' first...\n");
        if (cmd_install() != 0) {
            return 1;
        }
        if (lockfile_read("cpm.lock", &lock) != 0) {
            fprintf(stderr, "Error: Could not read cpm.lock\n");
            return 1;
        }
    }

    ProjectConfig config;
    manifest_read("cpm.toml", &config);

    printf("Auditing dependencies for '%s' against known vulnerability advisories...\n\n",
           config.name[0] ? config.name : "project");

    int vulns_found = 0;

    for (int i = 0; i < lock.package_count; i++) {
        const LockedPackage *p = &lock.packages[i];

        // Determine version to check
        char check_ver[MAX_VER_LEN] = {0};
        if (p->version[0]) {
            strncpy(check_ver, p->version, sizeof(check_ver) - 1);
        } else if (p->tag[0]) {
            strncpy(check_ver, p->tag, sizeof(check_ver) - 1);
        }

        int pkg_vulns = 0;

        for (int v = 0; v < VULN_COUNT; v++) {
            const Vulnerability *vuln = &VULN_DB[v];
            if (strcmp(p->name, vuln->package_name) == 0) {
                if (check_ver[0] && semver_satisfies(vuln->affected_constraint, check_ver)) {
                    printf("  [VULNERABLE] %s (%s)\n", p->name, check_ver);
                    printf("    Advisory:    %s [%s]\n", vuln->cve_id, vuln->severity);
                    printf("    Condition:   %s\n", vuln->affected_constraint);
                    printf("    Details:     %s\n", vuln->description);
                    pkg_vulns++;
                    vulns_found++;
                }
            }
        }

        if (pkg_vulns == 0) {
            printf("  [OK] %s (%s) - No known vulnerabilities\n",
                   p->name, check_ver[0] ? check_ver : "unversioned");
        }
    }

    printf("\nAudit Summary: %d vulnerabilities detected across %d dependencies.\n",
           vulns_found, lock.package_count);

    if (generate_sbom) {
        export_sbom(&lock, &config);
    } else {
        printf("Tip: Generate an SBOM with: cpm audit --sbom\n");
    }

    return (vulns_found == 0) ? 0 : 1;
}
