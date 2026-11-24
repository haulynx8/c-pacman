#define _POSIX_C_SOURCE 200809L
#include "cpm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#define CPM_DEFAULT_REGISTRY_URL "https://github.com/cpm-packages/registry.git"

static RegistryEntry g_registry[MAX_REGISTRY];
static int g_registry_count = 0;
static int g_initialized = 0;

// Find the directory where the cpm executable is located
static void get_binary_dir(char *out, size_t sz) {
    char exe_path[MAX_PATH_LEN] = {0};
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len > 0) {
        exe_path[len] = '\0';
        char *slash = strrchr(exe_path, '/');
        if (slash) *slash = '\0';
        strncpy(out, exe_path, sz - 1);
        out[sz - 1] = '\0';
    } else {
        out[0] = '\0';
    }
}

// Parse a single registry .toml file
static int registry_parse_file(const char *filepath, RegistryEntry *entry) {
    FILE *fp = fopen(filepath, "r");
    if (!fp) return -1;

    memset(entry, 0, sizeof(RegistryEntry));
    entry->type = DEP_TYPE_GIT; // default

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *p = line;
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p == '#' || *p == '[' || *p == '\0') continue;

        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';

        char *key = p;
        char *val = eq + 1;
        trim_whitespace(key);
        trim_whitespace(val);

        if (strcmp(key, "name") == 0) {
            strncpy(entry->name, val, sizeof(entry->name) - 1);
        } else if (strcmp(key, "description") == 0) {
            strncpy(entry->description, val, sizeof(entry->description) - 1);
        } else if (strcmp(key, "type") == 0) {
            if (strcmp(val, "url") == 0) entry->type = DEP_TYPE_URL;
            else if (strcmp(val, "system") == 0) entry->type = DEP_TYPE_SYSTEM;
            else entry->type = DEP_TYPE_GIT;
        } else if (strcmp(key, "source") == 0 || strcmp(key, "url") == 0) {
            strncpy(entry->url, val, sizeof(entry->url) - 1);
        } else if (strcmp(key, "tag") == 0) {
            strncpy(entry->tag, val, sizeof(entry->tag) - 1);
        } else if (strcmp(key, "version") == 0) {
            strncpy(entry->version, val, sizeof(entry->version) - 1);
        }
    }

    fclose(fp);
    return (entry->name[0] != '\0') ? 0 : -1;
}

// Load all .toml files from a directory into memory
int registry_load_dir(const char *dir_path) {
    DIR *dir = opendir(dir_path);
    if (!dir) return 0;

    int loaded = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        size_t len = strlen(entry->d_name);
        if (len < 6 || strcmp(entry->d_name + len - 5, ".toml") != 0) continue;
        if (g_registry_count >= MAX_REGISTRY) break;

        char path[MAX_PATH_LEN];
        snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);

        RegistryEntry item;
        if (registry_parse_file(path, &item) == 0) {
            // Check if already in memory (don't duplicate)
            int exists = 0;
            for (int i = 0; i < g_registry_count; i++) {
                if (strcasecmp(g_registry[i].name, item.name) == 0) {
                    exists = 1;
                    break;
                }
            }
            if (!exists) {
                g_registry[g_registry_count++] = item;
                loaded++;
            }
        }
    }

    closedir(dir);
    return loaded;
}

// Initialize registry by searching ~/.cpm/registry, ./registry, and <cpm_dir>/registry
int registry_init(void) {
    if (g_initialized) return g_registry_count;
    g_initialized = 1;

    // 1. Try user home directory index (~/.cpm/registry)
    const char *home = getenv("HOME");
    if (home) {
        char user_reg[MAX_PATH_LEN];
        snprintf(user_reg, sizeof(user_reg), "%s/.cpm/registry", home);
        registry_load_dir(user_reg);
    }

    // 2. Try local project registry folder (./registry)
    registry_load_dir("registry");

    // 3. Try registry folder adjacent to the cpm binary
    char bin_dir[MAX_PATH_LEN];
    get_binary_dir(bin_dir, sizeof(bin_dir));
    if (bin_dir[0]) {
        char bin_reg[MAX_PATH_LEN];
        snprintf(bin_reg, sizeof(bin_reg), "%s/registry", bin_dir);
        registry_load_dir(bin_reg);
    }

    return g_registry_count;
}

// Case-insensitive substring helper
static int contains_case_insensitive(const char *haystack, const char *needle) {
    if (!needle || needle[0] == '\0') return 1;
    if (!haystack) return 0;

    size_t hlen = strlen(haystack);
    size_t nlen = strlen(needle);
    if (nlen > hlen) return 0;

    for (size_t i = 0; i <= hlen - nlen; i++) {
        size_t j = 0;
        while (j < nlen && tolower((unsigned char)haystack[i + j]) == tolower((unsigned char)needle[j])) {
            j++;
        }
        if (j == nlen) return 1;
    }
    return 0;
}

// Find a package in the registry by exact name (case-insensitive)
const RegistryEntry *registry_find(const char *name) {
    registry_init();
    if (!name) return NULL;

    for (int i = 0; i < g_registry_count; i++) {
        if (strcasecmp(g_registry[i].name, name) == 0) {
            return &g_registry[i];
        }
    }
    return NULL;
}

// Save a newly discovered package (e.g. from owner/repo) into ~/.cpm/registry/
int registry_save_entry(const RegistryEntry *entry) {
    const char *home = getenv("HOME");
    if (!home) return -1;

    char dir[MAX_PATH_LEN];
    snprintf(dir, sizeof(dir), "%s/.cpm/registry", home);

    char mkdir_cmd[MAX_PATH_LEN + 16];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s", dir);
    system(mkdir_cmd);

    char filename[MAX_PATH_LEN + 128];
    // Lowercase name for file
    char clean_name[MAX_NAME_LEN];
    for (size_t i = 0; i < sizeof(clean_name) - 1 && entry->name[i]; i++) {
        clean_name[i] = (char)tolower((unsigned char)entry->name[i]);
        clean_name[i + 1] = '\0';
    }

    snprintf(filename, sizeof(filename), "%s/%s.toml", dir, clean_name);
    FILE *fp = fopen(filename, "w");
    if (!fp) return -1;

    fprintf(fp, "[package]\n");
    fprintf(fp, "name = \"%s\"\n", entry->name);
    fprintf(fp, "description = \"%s\"\n", entry->description[0] ? entry->description : "Community package");
    fprintf(fp, "type = \"%s\"\n", (entry->type == DEP_TYPE_URL) ? "url" : ((entry->type == DEP_TYPE_SYSTEM) ? "system" : "git"));
    if (entry->url[0]) fprintf(fp, "source = \"%s\"\n", entry->url);
    if (entry->tag[0]) fprintf(fp, "tag = \"%s\"\n", entry->tag);
    if (entry->version[0]) fprintf(fp, "version = \"%s\"\n", entry->version);

    fclose(fp);

    // Also update in-memory registry
    if (g_registry_count < MAX_REGISTRY) {
        g_registry[g_registry_count++] = *entry;
    }

    return 0;
}

static const char *type_to_string(DepType type) {
    switch (type) {
        case DEP_TYPE_GIT: return "git";
        case DEP_TYPE_URL: return "url";
        case DEP_TYPE_SYSTEM: return "system";
        default: return "unknown";
    }
}

int cmd_search(const char *query) {
    registry_init();

    printf("Searching cpm package registry for '%s'...\n\n", (query && query[0]) ? query : "*");
    printf("  %-16s %-10s %-8s %s\n", "NAME", "VERSION", "TYPE", "DESCRIPTION");
    printf("  --------------------------------------------------------------------------------\n");

    int matches = 0;
    for (int i = 0; i < g_registry_count; i++) {
        const RegistryEntry *e = &g_registry[i];
        if (!query || query[0] == '\0' || strcmp(query, "*") == 0 ||
            contains_case_insensitive(e->name, query) ||
            contains_case_insensitive(e->description, query)) {

            const char *ver = e->version[0] ? e->version : (e->tag[0] ? e->tag : "latest");
            printf("  %-16s %-10s %-8s %s\n",
                   e->name, ver, type_to_string(e->type), e->description);
            matches++;
        }
    }

    printf("\nFound %d package%s in local registry.\n", matches, matches == 1 ? "" : "s");
    printf("Tip: Add packages with 'cpm add <name>' or 'cpm add owner/repo'.\n");
    printf("     Sync latest registry catalog with 'cpm sync'.\n");

    return 0;
}

// Sync ~/.cpm/registry index
int cmd_sync(void) {
    const char *home = getenv("HOME");
    if (!home) {
        fprintf(stderr, "Error: HOME environment variable unset.\n");
        return 1;
    }

    char dir[MAX_PATH_LEN];
    snprintf(dir, sizeof(dir), "%s/.cpm/registry", home);

    char bin_dir[MAX_PATH_LEN];
    get_binary_dir(bin_dir, sizeof(bin_dir));

    char cmd[MAX_CMD_LEN];
    struct stat st;
    if (stat(dir, &st) == 0 && S_ISDIR(st.st_mode)) {
        printf("[cpm] Updating registry index in %s...\n", dir);
        char git_dir[MAX_PATH_LEN + 32];
        snprintf(git_dir, sizeof(git_dir), "%s/.git", dir);
        if (stat(git_dir, &st) == 0) {
            snprintf(cmd, sizeof(cmd), "git -C %s pull --quiet", dir);
            system(cmd);
        } else {
            snprintf(cmd, sizeof(cmd), "mkdir -p %s && cp -r %s/registry/*.toml %s/ 2>/dev/null", dir, bin_dir, dir);
            system(cmd);
        }
    } else {
        printf("[cpm] Initializing local registry catalog in %s...\n", dir);
        snprintf(cmd, sizeof(cmd), "mkdir -p %s && cp -r %s/registry/*.toml %s/ 2>/dev/null", dir, bin_dir, dir);
        system(cmd);
    }

    // Reload registry
    g_registry_count = 0;
    g_initialized = 0;
    int total = registry_init();
    printf("[cpm] Registry index synchronized! (%d packages loaded)\n", total);
    return 0;
}
