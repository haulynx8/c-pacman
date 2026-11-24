#include "cpm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int cmd_init(const char *project_name) {
    char name[MAX_NAME_LEN];
    if (project_name && strlen(project_name) > 0) {
        snprintf(name, sizeof(name), "%s", project_name);
    } else {
        snprintf(name, sizeof(name), "my_project");
    }

    // Check if cpm.toml already exists
    FILE *check = fopen("cpm.toml", "r");
    if (check) {
        fclose(check);
        fprintf(stderr, "Error: cpm.toml already exists in this folder!\n");
        return 1;
    }

    // Set up default config
    ProjectConfig config;
    memset(&config, 0, sizeof(config));
    snprintf(config.name, sizeof(config.name), "%s", name);
    snprintf(config.version, sizeof(config.version), "0.1.0");
    snprintf(config.standard, sizeof(config.standard), "c17");
    config.dep_count = 0;

    if (manifest_write("cpm.toml", &config) != 0) {
        fprintf(stderr, "Error: Could not create cpm.toml\n");
        return 1;
    }

    // Make starter directories
    system("mkdir -p src include tests");

    // Create a starter main.c if it doesn't exist
    FILE *src_main = fopen("src/main.c", "r");
    if (!src_main) {
        FILE *new_main = fopen("src/main.c", "w");
        if (new_main) {
            fprintf(new_main, "#include <stdio.h>\n\n");
            fprintf(new_main, "int main(void) {\n");
            fprintf(new_main, "    printf(\"Hello from %s!\\n\");\n", name);
            fprintf(new_main, "    return 0;\n");
            fprintf(new_main, "}\n");
            fclose(new_main);
        }
    } else {
        fclose(src_main);
    }

    printf("[cpm] Created project '%s'!\n", name);
    printf("  Created cpm.toml, src/main.c, include/, tests/\n");
    printf("  Try running 'cpm build' next.\n");
    return 0;
}

static void print_usage(void) {
    printf("cpm - C Package Manager (v%s)\n", CPM_VERSION);
    printf("A Cargo-like tool for modern C projects.\n\n");
    printf("Usage:\n");
    printf("  cpm init [project-name]              Initialize a new C project\n");
    printf("  cpm sync                             Sync local package index from registry repo\n");
    printf("  cpm search [query]                   Search packages in the registry\n");
    printf("  cpm add <pkg>                        Add a package (from registry or pkg-config)\n");
    printf("  cpm add <owner/repo>                 Add any GitHub repo directly (resolves tags)\n");
    printf("  cpm add <pkg> --git <url> [--tag <t>] Add a Git repository dependency\n");
    printf("  cpm add <pkg> --url <url>            Add a single-header / direct URL\n");
    printf("  cpm install                          Resolve and download all dependencies\n");
    printf("  cpm update                           Update dependencies to latest versions\n");
    printf("  cpm build                            Compile the project\n");
    printf("  cpm test                             Run test suite in tests/\n");
    printf("  cpm audit [--sbom]                   Vulnerability scan & generate CycloneDX SBOM\n");
    printf("  cpm clean [--all]                    Remove build artifacts (or everything with --all)\n");
    printf("  cpm help                             Show this help message\n\n");
    printf("Examples:\n");
    printf("  cpm sync\n");
    printf("  cpm search json\n");
    printf("  cpm add DaveGamble/cJSON             # Zero-config GitHub resolution!\n");
    printf("  cpm add libcurl                      # System pkg-config\n");
    printf("  cpm build\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "init") == 0) {
        const char *name = (argc >= 3) ? argv[2] : "my_c_project";
        return cmd_init(name);
    } else if (strcmp(cmd, "add") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Error: 'cpm add' requires a package name.\n");
            fprintf(stderr, "Example: cpm add libcurl\n");
            return 1;
        }

        const char *pkg_name = argv[2];
        DepType type = DEP_TYPE_SYSTEM;
        const char *url = NULL;
        const char *tag = NULL;

        for (int i = 3; i < argc; i++) {
            if (strcmp(argv[i], "--git") == 0 && i + 1 < argc) {
                type = DEP_TYPE_GIT;
                url = argv[++i];
            } else if (strcmp(argv[i], "--tag") == 0 && i + 1 < argc) {
                tag = argv[++i];
            } else if (strcmp(argv[i], "--url") == 0 && i + 1 < argc) {
                type = DEP_TYPE_URL;
                url = argv[++i];
            }
        }

        char target_name[MAX_NAME_LEN];
        strncpy(target_name, pkg_name, sizeof(target_name) - 1);
        target_name[sizeof(target_name) - 1] = '\0';

        char dynamic_url[MAX_PATH_LEN] = {0};
        char dynamic_tag[MAX_VER_LEN] = {0};

        if (type == DEP_TYPE_SYSTEM) {
            // 1. Check local registry index (.toml files)
            const RegistryEntry *reg = registry_find(target_name);
            if (reg) {
                printf("[cpm] Found '%s' in registry: %s\n", reg->name, reg->description);
                type = reg->type;
                if (reg->url[0]) {
                    strncpy(dynamic_url, reg->url, sizeof(dynamic_url) - 1);
                    url = dynamic_url;
                }
                if (reg->tag[0]) {
                    strncpy(dynamic_tag, reg->tag, sizeof(dynamic_tag) - 1);
                    tag = dynamic_tag;
                }
            } else if (strchr(target_name, '/') != NULL) {
                // 2. Fix 3: owner/repo shorthand with zero registry entry!
                type = DEP_TYPE_GIT;
                snprintf(dynamic_url, sizeof(dynamic_url), "https://github.com/%s.git", target_name);
                url = dynamic_url;

                const char *slash = strchr(target_name, '/');
                char repo_name[MAX_NAME_LEN];
                strncpy(repo_name, slash + 1, sizeof(repo_name) - 1);
                repo_name[sizeof(repo_name) - 1] = '\0';

                printf("[cpm] Detected GitHub repository shorthand: %s\n", target_name);
                printf("[cpm] Querying remote release tags via git ls-remote...\n");

                if (find_latest_tag(dynamic_url, dynamic_tag, sizeof(dynamic_tag))) {
                    printf("[cpm] Selected newest release tag: %s\n", dynamic_tag);
                    tag = dynamic_tag;
                } else {
                    printf("[cpm] No release tags found; tracking default branch.\n");
                }

                // Fix 5: Auto-cache resolved package into ~/.cpm/registry/<repo>.toml!
                RegistryEntry new_reg;
                memset(&new_reg, 0, sizeof(new_reg));
                strncpy(new_reg.name, repo_name, sizeof(new_reg.name) - 1);
                strncpy(new_reg.description, "Resolved from GitHub repository", sizeof(new_reg.description) - 1);
                new_reg.type = DEP_TYPE_GIT;
                strncpy(new_reg.url, dynamic_url, sizeof(new_reg.url) - 1);
                if (dynamic_tag[0]) strncpy(new_reg.tag, dynamic_tag, sizeof(new_reg.tag) - 1);
                registry_save_entry(&new_reg);

                strncpy(target_name, repo_name, sizeof(target_name) - 1);
            }
        }

        return cmd_add(target_name, type, url, tag);
    } else if (strcmp(cmd, "sync") == 0) {
        return cmd_sync();
    } else if (strcmp(cmd, "search") == 0) {
        const char *query = (argc >= 3) ? argv[2] : "";
        return cmd_search(query);
    } else if (strcmp(cmd, "install") == 0) {
        return cmd_install();
    } else if (strcmp(cmd, "update") == 0) {
        return cmd_update();
    } else if (strcmp(cmd, "audit") == 0) {
        int sbom = (argc >= 3 && strcmp(argv[2], "--sbom") == 0);
        return cmd_audit(sbom);
    } else if (strcmp(cmd, "clean") == 0) {
        int all = (argc >= 3 && strcmp(argv[2], "--all") == 0);
        return cmd_clean(all);
    } else if (strcmp(cmd, "build") == 0) {
        return cmd_build();
    } else if (strcmp(cmd, "test") == 0) {
        return cmd_test();
    } else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        print_usage();
        return 0;
    } else if (strcmp(cmd, "--version") == 0 || strcmp(cmd, "-v") == 0) {
        printf("cpm version %s\n", CPM_VERSION);
        return 0;
    } else {
        fprintf(stderr, "Error: Unknown command '%s'\n", cmd);
        print_usage();
        return 1;
    }
}
