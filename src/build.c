#define _POSIX_C_SOURCE 200809L
#include "cpm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>

int detect_compiler(char *out_compiler, size_t max_len) {
    const char *env_cc = getenv("CC");
    if (env_cc && strlen(env_cc) > 0) {
        snprintf(out_compiler, max_len, "%s", env_cc);
        return 0;
    }

    if (system("which gcc >/dev/null 2>&1") == 0) {
        snprintf(out_compiler, max_len, "gcc");
        return 0;
    }

    if (system("which clang >/dev/null 2>&1") == 0) {
        snprintf(out_compiler, max_len, "clang");
        return 0;
    }

    return -1;
}

int cmd_build(void) {
    ProjectConfig config;
    if (manifest_read("cpm.toml", &config) != 0) {
        fprintf(stderr, "Error: cpm.toml not found. Run 'cpm init' first.\n");
        return 1;
    }

    char compiler[64];
    if (detect_compiler(compiler, sizeof(compiler)) != 0) {
        fprintf(stderr, "Error: Could not find gcc or clang on PATH, and $CC is unset.\n");
        return 1;
    }

    // Check if .cpm/flags.txt exists, if not run install first
    FILE *flags_fp = fopen(".cpm/flags.txt", "r");
    if (!flags_fp) {
        printf("[cpm] Flags cache missing, running 'cpm install' first...\n");
        if (cmd_install() != 0) {
            return 1;
        }
        flags_fp = fopen(".cpm/flags.txt", "r");
    }

    char flags[1024] = {0};
    if (flags_fp) {
        if (fgets(flags, sizeof(flags), flags_fp)) {
            flags[strcspn(flags, "\r\n")] = '\0';
        }
        fclose(flags_fp);
    }

    // Collect any compiled dependency object files in .cpm/obj/*.o and static libraries *.a
    char obj_files[1024] = "";
    DIR *d = opendir(".cpm/obj");
    if (d) {
        struct dirent *dir;
        while ((dir = readdir(d)) != NULL) {
            if (strstr(dir->d_name, ".o") || strstr(dir->d_name, ".a")) {
                strncat(obj_files, " .cpm/obj/", sizeof(obj_files) - strlen(obj_files) - 1);
                strncat(obj_files, dir->d_name, sizeof(obj_files) - strlen(obj_files) - 1);
            }
        }
        closedir(d);
    }

    // Ensure output directory exists
    system("mkdir -p bin");

    // Standard flag (e.g. -std=c17)
    char std_flag[32] = "-std=c17";
    if (strlen(config.standard) > 0) {
        snprintf(std_flag, sizeof(std_flag), "-std=%s", config.standard);
    }

    char cmd[MAX_CMD_LEN];
    snprintf(cmd, sizeof(cmd), "%s %s -Wall -Wextra -Iinclude src/*.c%s -o bin/%s %s",
             compiler, std_flag, obj_files, config.name, flags);

    printf("[cpm] Compiling with %s...\n", compiler);
    printf("  $ %s\n", cmd);

    int ret = system(cmd);
    if (ret == 0) {
        printf("[cpm] Build succeeded! Executable: bin/%s\n", config.name);
    } else {
        fprintf(stderr, "[cpm] Compilation failed (exit code %d).\n", ret);
    }

    return ret;
}

int cmd_test(void) {
    ProjectConfig config;
    if (manifest_read("cpm.toml", &config) != 0) {
        fprintf(stderr, "Error: cpm.toml not found.\n");
        return 1;
    }

    struct stat st;
    if (stat("tests", &st) != 0 || !S_ISDIR(st.st_mode)) {
        printf("[cpm] No 'tests/' directory found. Create 'tests/test_main.c' to add unit tests!\n");
        return 0;
    }

    char compiler[64];
    if (detect_compiler(compiler, sizeof(compiler)) != 0) {
        fprintf(stderr, "Error: No compiler found.\n");
        return 1;
    }

    char flags[1024] = {0};
    FILE *flags_fp = fopen(".cpm/flags.txt", "r");
    if (flags_fp) {
        if (fgets(flags, sizeof(flags), flags_fp)) {
            flags[strcspn(flags, "\r\n")] = '\0';
        }
        fclose(flags_fp);
    }

    char obj_files[1024] = "";
    DIR *d = opendir(".cpm/obj");
    if (d) {
        struct dirent *dir;
        while ((dir = readdir(d)) != NULL) {
            if (strstr(dir->d_name, ".o") || strstr(dir->d_name, ".a")) {
                strncat(obj_files, " .cpm/obj/", sizeof(obj_files) - strlen(obj_files) - 1);
                strncat(obj_files, dir->d_name, sizeof(obj_files) - strlen(obj_files) - 1);
            }
        }
        closedir(d);
    }

    char cmd[MAX_CMD_LEN];
    snprintf(cmd, sizeof(cmd), "%s -Iinclude tests/*.c src/semver.c src/manifest.c%s -o bin/test_runner %s",
             compiler, obj_files, flags);

    printf("[cpm] Building test runner...\n");
    printf("  $ %s\n", cmd);

    int build_res = system(cmd);
    if (build_res != 0) {
        fprintf(stderr, "[cpm] Test compilation failed.\n");
        return build_res;
    }

    printf("[cpm] Running tests:\n");
    return system("./bin/test_runner");
}
