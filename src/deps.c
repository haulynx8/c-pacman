#define _POSIX_C_SOURCE 200809L
#include "cpm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>

// Ask remote Git repository for tags and pick the newest SemVer release
int find_latest_tag(const char *git_url, char *out_tag, size_t out_sz) {
    out_tag[0] = '\0';
    char cmd[MAX_CMD_LEN];
    snprintf(cmd, sizeof(cmd), "git ls-remote --tags --refs %s 2>/dev/null", git_url);

    FILE *fp = popen(cmd, "r");
    if (!fp) return 0;

    char line[512];
    SemVer best, cur;
    int have_best = 0;

    while (fgets(line, sizeof(line), fp)) {
        char *tag = strstr(line, "refs/tags/");
        if (!tag) continue;
        tag += strlen("refs/tags/");
        tag[strcspn(tag, "\r\n")] = '\0';

        if (semver_parse(tag, &cur) != 0) continue; // skip non-semver tags like "nightly"
        if (cur.has_prerelease) continue;          // skip betas/prereleases

        if (!have_best || semver_compare(&cur, &best) > 0) {
            best = cur;
            have_best = 1;
            strncpy(out_tag, tag, out_sz - 1);
            out_tag[out_sz - 1] = '\0';
        }
    }
    pclose(fp);
    return have_best;
}

// Helpers for layout detection
static int file_exists_in(const char *dir, const char *filename) {
    char path[MAX_PATH_LEN];
    snprintf(path, sizeof(path), "%s/%s", dir, filename);
    struct stat st;
    return (stat(path, &st) == 0 && S_ISREG(st.st_mode));
}

static int count_files_with_ext(const char *dir, const char *ext) {
    DIR *d = opendir(dir);
    if (!d) return 0;
    int count = 0;
    struct dirent *de;
    size_t elen = strlen(ext);
    while ((de = readdir(d)) != NULL) {
        size_t nlen = strlen(de->d_name);
        if (nlen >= elen && strcmp(de->d_name + nlen - elen, ext) == 0) {
            count++;
        }
    }
    closedir(d);
    return count;
}

// Inspect package folder to decide whether it is Plain C, CMake, Make, or Header-only
Layout detect_layout(const char *dir) {
    // 1. If it has C source files in root, compile directly (fast & reliable for cJSON, sqlite, etc.)
    if (count_files_with_ext(dir, ".c") > 0) return LAYOUT_PLAIN_C;

    // 2. Build system delegation
    if (file_exists_in(dir, "CMakeLists.txt")) return LAYOUT_CMAKE;
    if (file_exists_in(dir, "Makefile"))       return LAYOUT_MAKE;

    // 3. Nested src/ folder with C files
    char src_dir[MAX_PATH_LEN];
    snprintf(src_dir, sizeof(src_dir), "%s/src", dir);
    if (count_files_with_ext(src_dir, ".c") > 0) return LAYOUT_PLAIN_C;

    // 4. Header-only
    if (count_files_with_ext(dir, ".h") > 0) return LAYOUT_HEADER_ONLY;

    char inc_dir[MAX_PATH_LEN];
    snprintf(inc_dir, sizeof(inc_dir), "%s/include", dir);
    if (count_files_with_ext(inc_dir, ".h") > 0) return LAYOUT_HEADER_ONLY;

    return LAYOUT_UNKNOWN;
}

// Try to ask pkg-config if a library is installed on the system (e.g. libcurl, openssl)
int check_pkg_config(const char *dep_name, char *cflags, size_t cflags_sz, char *libs, size_t libs_sz) {
    char cmd[512];

    snprintf(cmd, sizeof(cmd), "pkg-config --exists %s 2>/dev/null", dep_name);
    if (system(cmd) != 0) {
        return 0; // Not found
    }

    snprintf(cmd, sizeof(cmd), "pkg-config --cflags %s 2>/dev/null", dep_name);
    FILE *fp = popen(cmd, "r");
    if (fp) {
        if (fgets(cflags, cflags_sz, fp)) {
            cflags[strcspn(cflags, "\r\n")] = '\0';
        }
        pclose(fp);
    }

    snprintf(cmd, sizeof(cmd), "pkg-config --libs %s 2>/dev/null", dep_name);
    fp = popen(cmd, "r");
    if (fp) {
        if (fgets(libs, libs_sz, fp)) {
            libs[strcspn(libs, "\r\n")] = '\0';
        }
        pclose(fp);
    }

    return 1;
}

int cmd_add(const char *dep_name, DepType type, const char *url, const char *tag) {
    ProjectConfig config;
    if (manifest_read("cpm.toml", &config) != 0) {
        fprintf(stderr, "Error: Could not find cpm.toml in current directory.\n");
        fprintf(stderr, "Hint: Run 'cpm init' first!\n");
        return 1;
    }

    char name[MAX_NAME_LEN];
    char ver[MAX_VER_LEN] = "latest";

    // Handle name@version shorthand if not using flags
    const char *at = strchr(dep_name, '@');
    if (at && type == DEP_TYPE_SYSTEM) {
        size_t nlen = at - dep_name;
        if (nlen >= sizeof(name)) nlen = sizeof(name) - 1;
        strncpy(name, dep_name, nlen);
        name[nlen] = '\0';
        snprintf(ver, sizeof(ver), "%s", at + 1);
    } else {
        snprintf(name, sizeof(name), "%s", dep_name);
    }

    // Check if already in dependencies
    int found_idx = -1;
    for (int i = 0; i < config.dep_count; i++) {
        if (strcmp(config.deps[i].name, name) == 0) {
            found_idx = i;
            break;
        }
    }

    Dependency *target = NULL;
    if (found_idx != -1) {
        target = &config.deps[found_idx];
        printf("[cpm] Updating existing dependency: %s\n", name);
    } else {
        if (config.dep_count >= MAX_DEPS) {
            fprintf(stderr, "Error: Reached maximum dependency limit (%d)\n", MAX_DEPS);
            return 1;
        }
        target = &config.deps[config.dep_count++];
    }

    memset(target, 0, sizeof(Dependency));
    strncpy(target->name, name, sizeof(target->name) - 1);
    target->type = type;

    if (type == DEP_TYPE_GIT) {
        strncpy(target->url, url ? url : "", sizeof(target->url) - 1);
        if (tag && strlen(tag) > 0) {
            strncpy(target->tag, tag, sizeof(target->tag) - 1);
        }
        printf("[cpm] Added Git dependency: %s -> %s\n", name, target->url);
    } else if (type == DEP_TYPE_URL) {
        strncpy(target->url, url ? url : "", sizeof(target->url) - 1);
        printf("[cpm] Added URL dependency: %s -> %s\n", name, target->url);
    } else {
        strncpy(target->version, ver, sizeof(target->version) - 1);
        printf("[cpm] Added system dependency: %s (%s)\n", name, ver);
    }

    if (manifest_write("cpm.toml", &config) != 0) {
        fprintf(stderr, "Error: Failed to write to cpm.toml\n");
        return 1;
    }

    // Automatically resolve and install after adding
    return cmd_install();
}

int cmd_install(void) {
    ProjectConfig config;
    if (manifest_read("cpm.toml", &config) != 0) {
        fprintf(stderr, "Error: cpm.toml not found. Did you run 'cpm init'?\n");
        return 1;
    }

    printf("[cpm] Resolving dependencies for %s...\n", config.name);

    // Read existing lockfile if one was committed to git
    Lockfile existing_lock;
    int has_lock = (lockfile_read("cpm.lock", &existing_lock) == 0);

    // Prepare fresh lockfile to record exact state
    Lockfile new_lock;
    memset(&new_lock, 0, sizeof(Lockfile));
    new_lock.version = 1;

    // Ensure cache folders exist
    system("mkdir -p .cpm/deps .cpm/obj");

    FILE *flags_fp = fopen(".cpm/flags.txt", "w");
    if (!flags_fp) {
        perror("fopen .cpm/flags.txt");
        return 1;
    }

    int resolved_count = 0;

    for (int i = 0; i < config.dep_count; i++) {
        Dependency *dep = &config.deps[i];
        const LockedPackage *locked = has_lock ? lockfile_find(&existing_lock, dep->name) : NULL;

        if (dep->type == DEP_TYPE_GIT) {
            char dep_dir[MAX_PATH_LEN];
            snprintf(dep_dir, sizeof(dep_dir), ".cpm/deps/%s", dep->name);

            struct stat st;
            if (stat(dep_dir, &st) != 0) {
                printf("  -> [git] Cloning %s from %s...\n", dep->name, dep->url);
                char clone_cmd[MAX_CMD_LEN];
                if (dep->tag[0]) {
                    snprintf(clone_cmd, sizeof(clone_cmd),
                             "git clone --depth 1 --branch %s %s %s >/dev/null 2>&1",
                             dep->tag, dep->url, dep_dir);
                } else {
                    snprintf(clone_cmd, sizeof(clone_cmd),
                             "git clone --depth 1 %s %s >/dev/null 2>&1",
                             dep->url, dep_dir);
                }

                if (system(clone_cmd) != 0) {
                    fprintf(stderr, "  [error] Failed to git clone %s\n", dep->url);
                    continue;
                }
            }

            // If cpm.lock specifies an exact commit hash, pin the repo to it
            if (locked && locked->commit[0]) {
                char checkout_cmd[MAX_CMD_LEN];
                snprintf(checkout_cmd, sizeof(checkout_cmd),
                         "git -C %s checkout %s >/dev/null 2>&1",
                         dep_dir, locked->commit);
                system(checkout_cmd);
                printf("  -> [git] %s locked to commit %.10s...\n", dep->name, locked->commit);
            } else {
                printf("  -> [git] %s (%s) checked out\n", dep->name, dep->tag[0] ? dep->tag : "HEAD");
            }

            // Get exact current commit hash to save in lockfile
            char current_commit[65] = {0};
            get_git_commit(dep_dir, current_commit, sizeof(current_commit));

            if (new_lock.package_count < MAX_DEPS) {
                LockedPackage *lp = &new_lock.packages[new_lock.package_count++];
                strncpy(lp->name, dep->name, sizeof(lp->name) - 1);
                lp->type = DEP_TYPE_GIT;
                strncpy(lp->source, dep->url, sizeof(lp->source) - 1);
                strncpy(lp->tag, dep->tag, sizeof(lp->tag) - 1);
                strncpy(lp->commit, current_commit, sizeof(lp->commit) - 1);
            }

            // Add include path
            char inc_dir[MAX_PATH_LEN + 32];
            snprintf(inc_dir, sizeof(inc_dir), "%s/include", dep_dir);
            if (stat(inc_dir, &st) == 0 && S_ISDIR(st.st_mode)) {
                fprintf(flags_fp, "-I%s ", inc_dir);
            }
            fprintf(flags_fp, "-I%s ", dep_dir);

            // Auto-detect package layout (Fix 4: CMake, Make, Plain C, Header-only)
            Layout layout = detect_layout(dep_dir);

            if (layout == LAYOUT_CMAKE) {
                printf("  -> [detect] %s uses CMake. Running cmake build...\n", dep->name);
                char cmake_cmd[MAX_CMD_LEN];
                snprintf(cmake_cmd, sizeof(cmake_cmd),
                         "cmake -S %s -B %s/build -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF >/dev/null 2>&1 && "
                         "cmake --build %s/build >/dev/null 2>&1 && "
                         "find %s/build -name \"*.a\" -exec cp {} .cpm/obj/ \\; 2>/dev/null",
                         dep_dir, dep_dir, dep_dir, dep_dir);
                system(cmake_cmd);

            } else if (layout == LAYOUT_MAKE) {
                printf("  -> [detect] %s uses Makefile. Running make...\n", dep->name);
                char make_cmd[MAX_CMD_LEN];
                snprintf(make_cmd, sizeof(make_cmd),
                         "make -C %s >/dev/null 2>&1 && "
                         "find %s -maxdepth 2 -name \"*.a\" -exec cp {} .cpm/obj/ \\; 2>/dev/null",
                         dep_dir, dep_dir);
                system(make_cmd);

            } else if (layout == LAYOUT_PLAIN_C) {
                printf("  -> [detect] %s has C source files. Compiling objects...\n", dep->name);
                // Compile any library .c files into .cpm/obj/*.o (skipping tests/examples/main)
                char build_dep_cmd[MAX_CMD_LEN];
                snprintf(build_dep_cmd, sizeof(build_dep_cmd),
                         "for f in %s/*.c %s/src/*.c; do "
                         "  if [ -f \"$f\" ]; then "
                         "    bname=$(basename \"$f\" .c); "
                         "    case \"$bname\" in "
                         "      test*|*test*|example*|*example*|demo*|*demo*) continue ;; "
                         "    esac; "
                         "    if grep -qE \"main[[:space:]]*\\(\" \"$f\"; then continue; fi; "
                         "    obj_name=\".cpm/obj/%s_${bname}.o\"; "
                         "    if [ ! -f \"$obj_name\" ]; then "
                         "      gcc -c -O2 -I%s -I%s/include \"$f\" -o \"$obj_name\" >/dev/null 2>&1; "
                         "    fi; "
                         "  fi; "
                         "done",
                         dep_dir, dep_dir, dep->name, dep_dir, dep_dir);
                system(build_dep_cmd);

            } else if (layout == LAYOUT_HEADER_ONLY) {
                printf("  -> [detect] %s is a header-only library.\n", dep->name);
            }

            resolved_count++;

        } else if (dep->type == DEP_TYPE_URL) {
            char dep_dir[MAX_PATH_LEN];
            snprintf(dep_dir, sizeof(dep_dir), ".cpm/deps/%s", dep->name);
            system("mkdir -p .cpm/deps");

            const char *slash = strrchr(dep->url, '/');
            char filename[128];
            if (slash && strlen(slash + 1) > 0) {
                snprintf(filename, sizeof(filename), "%s", slash + 1);
            } else {
                snprintf(filename, sizeof(filename), "%s.h", dep->name);
            }

            char target_file[MAX_PATH_LEN + 128];
            snprintf(target_file, sizeof(target_file), "%s/%s", dep_dir, filename);

            struct stat st;
            if (stat(target_file, &st) != 0) {
                char mkdir_cmd[MAX_PATH_LEN + 32];
                snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s", dep_dir);
                system(mkdir_cmd);

                printf("  -> [download] Fetching %s (%s)...\n", dep->name, filename);
                char curl_cmd[MAX_CMD_LEN];
                snprintf(curl_cmd, sizeof(curl_cmd), "curl -fsSL '%s' -o '%s'", dep->url, target_file);
                if (system(curl_cmd) != 0) {
                    fprintf(stderr, "  [error] Failed to download %s\n", dep->url);
                    continue;
                }
            }

            // Verify sha256 checksum
            char current_sha[70] = {0};
            get_file_sha256(target_file, current_sha, sizeof(current_sha));

            if (locked && locked->checksum[0]) {
                if (strcmp(locked->checksum, current_sha) != 0) {
                    fprintf(stderr, "  [warning] CHECKSUM MISMATCH for %s!\n", dep->name);
                    fprintf(stderr, "    Locked:  %s\n", locked->checksum);
                    fprintf(stderr, "    Actual:  %s\n", current_sha);
                } else {
                    printf("  -> [download] %s checksum verified (%.10s...)\n", dep->name, current_sha);
                }
            } else {
                printf("  -> [download] %s sha256: %.10s...\n", dep->name, current_sha);
            }

            if (new_lock.package_count < MAX_DEPS) {
                LockedPackage *lp = &new_lock.packages[new_lock.package_count++];
                strncpy(lp->name, dep->name, sizeof(lp->name) - 1);
                lp->type = DEP_TYPE_URL;
                strncpy(lp->source, dep->url, sizeof(lp->source) - 1);
                strncpy(lp->checksum, current_sha, sizeof(lp->checksum) - 1);
            }

            fprintf(flags_fp, "-I%s ", dep_dir);
            resolved_count++;

        } else {
            // DEP_TYPE_SYSTEM: pkg-config check
            char cflags[256] = {0};
            char libs[256] = {0};

            if (check_pkg_config(dep->name, cflags, sizeof(cflags), libs, sizeof(libs))) {
                char sys_ver[MAX_VER_LEN] = {0};
                get_pkg_version(dep->name, sys_ver, sizeof(sys_ver));

                // Semver constraint evaluation
                if (strlen(dep->version) > 0 && strcmp(dep->version, "latest") != 0 && strlen(sys_ver) > 0) {
                    if (semver_satisfies(dep->version, sys_ver)) {
                        printf("  -> [system] %s (%s) satisfies constraint '%s'\n",
                               dep->name, sys_ver, dep->version);
                    } else {
                        fprintf(stderr, "  [warning] System package %s (%s) DOES NOT satisfy '%s'!\n",
                                dep->name, sys_ver, dep->version);
                    }
                } else {
                    printf("  -> [system] %s (%s) detected via pkg-config\n",
                           dep->name, sys_ver[0] ? sys_ver : dep->version);
                }

                if (strlen(cflags) > 0) fprintf(flags_fp, "%s ", cflags);
                if (strlen(libs) > 0) fprintf(flags_fp, "%s ", libs);

                if (new_lock.package_count < MAX_DEPS) {
                    LockedPackage *lp = &new_lock.packages[new_lock.package_count++];
                    strncpy(lp->name, dep->name, sizeof(lp->name) - 1);
                    lp->type = DEP_TYPE_SYSTEM;
                    strncpy(lp->version, sys_ver[0] ? sys_ver : dep->version, sizeof(lp->version) - 1);
                }
                resolved_count++;
            } else {
                printf("  -> [fallback] %s not in pkg-config, trying -l%s\n", dep->name, dep->name);
                if (strncmp(dep->name, "lib", 3) == 0) {
                    fprintf(flags_fp, "-l%s ", dep->name + 3);
                } else {
                    fprintf(flags_fp, "-l%s ", dep->name);
                }
                resolved_count++;
            }
        }
    }

    fprintf(flags_fp, "\n");
    fclose(flags_fp);

    // Save or update cpm.lock
    if (lockfile_write("cpm.lock", &new_lock) == 0) {
        printf("[cpm] Updated cpm.lock (%d package%s locked)\n",
               new_lock.package_count, new_lock.package_count == 1 ? "" : "s");
    }

    printf("[cpm] Ready! %d/%d dependencies configured in .cpm/\n", resolved_count, config.dep_count);
    return 0;
}
