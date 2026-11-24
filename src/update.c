#include "cpm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int cmd_update(void) {
    ProjectConfig config;
    if (manifest_read("cpm.toml", &config) != 0) {
        fprintf(stderr, "Error: cpm.toml not found. Did you run 'cpm init'?\n");
        return 1;
    }

    printf("[cpm] Updating dependencies for '%s' to latest compatible versions...\n", config.name);

    // Read previous lockfile so we can show diffs
    Lockfile old_lock;
    int had_old_lock = (lockfile_read("cpm.lock", &old_lock) == 0);

    // Remove existing cpm.lock so cmd_install() does not pin to old commits
    system("rm -f cpm.lock");

    // For any cached git repos, pull latest upstream updates
    for (int i = 0; i < config.dep_count; i++) {
        Dependency *dep = &config.deps[i];
        if (dep->type == DEP_TYPE_GIT) {
            char cmd[MAX_CMD_LEN];
            snprintf(cmd, sizeof(cmd), "if [ -d \".cpm/deps/%s/.git\" ]; then git -C .cpm/deps/%s pull --ff-only >/dev/null 2>&1; fi",
                     dep->name, dep->name);
            system(cmd);
        } else if (dep->type == DEP_TYPE_URL) {
            // Remove cached header file so curl re-downloads the latest
            char cmd[MAX_CMD_LEN];
            snprintf(cmd, sizeof(cmd), "rm -rf .cpm/deps/%s", dep->name);
            system(cmd);
        }
    }

    // Run clean install which will write a fresh cpm.lock
    if (cmd_install() != 0) {
        fprintf(stderr, "Error: Failed to reinstall updated dependencies.\n");
        return 1;
    }

    // Read new lockfile and print what changed
    Lockfile new_lock;
    if (lockfile_read("cpm.lock", &new_lock) == 0 && had_old_lock) {
        printf("\nUpdate Summary:\n");
        for (int i = 0; i < new_lock.package_count; i++) {
            const LockedPackage *np = &new_lock.packages[i];
            const LockedPackage *op = lockfile_find(&old_lock, np->name);
            if (op) {
                if (np->type == DEP_TYPE_GIT && strcmp(op->commit, np->commit) != 0) {
                    printf("  -> %s updated: %.10s -> %.10s\n", np->name, op->commit, np->commit);
                } else if (np->type == DEP_TYPE_SYSTEM && strcmp(op->version, np->version) != 0) {
                    printf("  -> %s updated: %s -> %s\n", np->name, op->version, np->version);
                } else {
                    printf("  -> %s up to date\n", np->name);
                }
            } else {
                printf("  -> %s added to lockfile\n", np->name);
            }
        }
    }

    printf("\n[cpm] Successfully updated cpm.lock!\n");
    return 0;
}
