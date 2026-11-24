#include "cpm.h"

#include <stdio.h>
#include <stdlib.h>

int cmd_clean(int all) {
    if (all) {
        printf("[cpm] Performing full clean (build artifacts, lockfile, and downloaded dependencies)...\n");
        system("rm -rf bin .cpm cpm.lock cpm.sbom.json");
        printf("[cpm] Cleaned everything! Run 'cpm install' to re-fetch and rebuild.\n");
    } else {
        printf("[cpm] Cleaning build artifacts and compiled object files...\n");
        system("rm -rf bin .cpm/obj .cpm/flags.txt cpm.sbom.json");
        printf("[cpm] Cleaned bin/ and .cpm/obj/. (Dependencies in .cpm/deps/ preserved)\n");
        printf("Tip: Run 'cpm clean --all' to also clear downloaded dependencies.\n");
    }
    return 0;
}
