#include "cpm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Helper to strip leading and trailing whitespace + quotes
void trim_whitespace(char *str) {
    if (!str) return;

    // Trim leading spaces
    char *start = str;
    while (*start && (isspace((unsigned char)*start) || *start == '"' || *start == '\'')) {
        start++;
    }

    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }

    // Trim trailing spaces and quotes
    int len = (int)strlen(str);
    while (len > 0 && (isspace((unsigned char)str[len - 1]) || str[len - 1] == '"' || str[len - 1] == '\'')) {
        str[len - 1] = '\0';
        len--;
    }
}

// Find a standalone key like 'git' or 'url' without matching substrings like 'github.com'
static const char *find_exact_key(const char *src, const char *key) {
    size_t klen = strlen(key);
    const char *p = src;
    while ((p = strstr(p, key)) != NULL) {
        int left_ok = (p == src || isspace((unsigned char)p[-1]) || p[-1] == '{' || p[-1] == ',');
        int right_ok = (isspace((unsigned char)p[klen]) || p[klen] == '=');
        if (left_ok && right_ok) {
            return p;
        }
        p++;
    }
    return NULL;
}

// Helper to extract a quoted value from something like: git = "https://..."
static void extract_field(const char *src, const char *field_name, char *dest, size_t dest_sz) {
    dest[0] = '\0';
    const char *found = find_exact_key(src, field_name);
    if (!found) return;

    const char *eq = strchr(found, '=');
    if (!eq) return;

    // Find opening quote
    const char *q1 = strchr(eq, '"');
    if (!q1) q1 = strchr(eq, '\'');
    if (!q1) return;

    // Find closing quote
    const char *q2 = strchr(q1 + 1, *q1);
    if (!q2) return;

    size_t len = q2 - (q1 + 1);
    if (len >= dest_sz) len = dest_sz - 1;
    strncpy(dest, q1 + 1, len);
    dest[len] = '\0';
}

int manifest_read(const char *filename, ProjectConfig *config) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        return -1;
    }

    memset(config, 0, sizeof(ProjectConfig));
    snprintf(config->standard, sizeof(config->standard), "c17");

    char line[1024];
    enum Section { SECTION_NONE, SECTION_PROJECT, SECTION_DEPS } current_section = SECTION_NONE;

    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = '\0';

        char *p = line;
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p == '#' || *p == '\0') {
            continue;
        }

        if (strstr(p, "[project]") == p) {
            current_section = SECTION_PROJECT;
            continue;
        } else if (strstr(p, "[dependencies]") == p) {
            current_section = SECTION_DEPS;
            continue;
        }

        char *eq = strchr(p, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = p;
        char *val = eq + 1;

        trim_whitespace(key);

        if (current_section == SECTION_PROJECT) {
            trim_whitespace(val);
            if (strcmp(key, "name") == 0) {
                strncpy(config->name, val, sizeof(config->name) - 1);
                config->name[sizeof(config->name) - 1] = '\0';
            } else if (strcmp(key, "version") == 0) {
                strncpy(config->version, val, sizeof(config->version) - 1);
                config->version[sizeof(config->version) - 1] = '\0';
            } else if (strcmp(key, "standard") == 0) {
                strncpy(config->standard, val, sizeof(config->standard) - 1);
                config->standard[sizeof(config->standard) - 1] = '\0';
            }
        } else if (current_section == SECTION_DEPS) {
            if (config->dep_count < MAX_DEPS) {
                Dependency *dep = &config->deps[config->dep_count];
                memset(dep, 0, sizeof(Dependency));
                strncpy(dep->name, key, sizeof(dep->name) - 1);
                dep->name[sizeof(dep->name) - 1] = '\0';

                char *brace = strchr(val, '{');
                if (brace) {
                    char git_url[MAX_PATH_LEN] = {0};
                    char direct_url[MAX_PATH_LEN] = {0};

                    extract_field(brace, "git", git_url, sizeof(git_url));
                    extract_field(brace, "url", direct_url, sizeof(direct_url));

                    if (git_url[0]) {
                        dep->type = DEP_TYPE_GIT;
                        strncpy(dep->url, git_url, sizeof(dep->url) - 1);
                        extract_field(brace, "tag", dep->tag, sizeof(dep->tag));
                    } else if (direct_url[0]) {
                        dep->type = DEP_TYPE_URL;
                        strncpy(dep->url, direct_url, sizeof(dep->url) - 1);
                    }
                } else {
                    trim_whitespace(val);
                    dep->type = DEP_TYPE_SYSTEM;
                    strncpy(dep->version, val, sizeof(dep->version) - 1);
                    dep->version[sizeof(dep->version) - 1] = '\0';
                }

                dep->is_system = 0;
                config->dep_count++;
            } else {
                printf("[warning] Reached max dependencies limit (%d)\n", MAX_DEPS);
            }
        }
    }

    fclose(fp);
    return 0;
}

int manifest_write(const char *filename, const ProjectConfig *config) {
    FILE *fp = fopen(filename, "w");
    if (!fp) {
        perror("fopen");
        return -1;
    }

    fprintf(fp, "[project]\n");
    fprintf(fp, "name = \"%s\"\n", config->name[0] ? config->name : "untitled");
    fprintf(fp, "version = \"%s\"\n", config->version[0] ? config->version : "0.1.0");
    fprintf(fp, "standard = \"%s\"\n\n", config->standard[0] ? config->standard : "c17");

    fprintf(fp, "[dependencies]\n");
    for (int i = 0; i < config->dep_count; i++) {
        const Dependency *dep = &config->deps[i];
        if (dep->type == DEP_TYPE_GIT) {
            if (dep->tag[0]) {
                fprintf(fp, "%s = { git = \"%s\", tag = \"%s\" }\n", dep->name, dep->url, dep->tag);
            } else {
                fprintf(fp, "%s = { git = \"%s\" }\n", dep->name, dep->url);
            }
        } else if (dep->type == DEP_TYPE_URL) {
            fprintf(fp, "%s = { url = \"%s\" }\n", dep->name, dep->url);
        } else {
            fprintf(fp, "%s = \"%s\"\n", dep->name, dep->version[0] ? dep->version : "latest");
        }
    }

    fclose(fp);
    return 0;
}
