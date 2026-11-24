#ifndef CPM_H
#define CPM_H

#include <stddef.h>

#define CPM_VERSION "0.7.0"
#define MAX_NAME_LEN 64
#define MAX_VER_LEN 32
#define MAX_PATH_LEN 512
#define MAX_DEPS 32
#define MAX_CMD_LEN 4096
#define MAX_REGISTRY 1024

// Type of dependency source
typedef enum {
    DEP_TYPE_SYSTEM = 0, // Resolved via pkg-config (e.g. libcurl, openssl)
    DEP_TYPE_GIT,        // Cloned from a Git repository
    DEP_TYPE_URL         // Single-header or file downloaded via curl
} DepType;

// Layout detection for third-party packages
typedef enum {
    LAYOUT_HEADER_ONLY = 0,
    LAYOUT_PLAIN_C,
    LAYOUT_MAKE,
    LAYOUT_CMAKE,
    LAYOUT_UNKNOWN
} Layout;

// Semantic Version representation
typedef struct {
    int major;
    int minor;
    int patch;
    char prerelease[32]; // e.g. "alpha.1"
    char build[32];      // e.g. "20240101"
    int has_prerelease;
    int has_build;
} SemVer;

// A single package dependency from cpm.toml
typedef struct {
    char name[MAX_NAME_LEN];
    char version[MAX_VER_LEN]; // e.g. "^8.0", ">=1.2.0", "latest"
    DepType type;
    char url[MAX_PATH_LEN];
    char tag[MAX_VER_LEN];
    int is_system; // 1 if detected through pkg-config on this machine
} Dependency;

// Project config parsed from cpm.toml
typedef struct {
    char name[MAX_NAME_LEN];
    char version[MAX_VER_LEN];
    char standard[16]; // e.g. "c17", "c11", "c99"
    Dependency deps[MAX_DEPS];
    int dep_count;
} ProjectConfig;

// Locked package record for cpm.lock
typedef struct {
    char name[MAX_NAME_LEN];
    DepType type;
    char version[MAX_VER_LEN];
    char source[MAX_PATH_LEN];
    char tag[MAX_VER_LEN];
    char commit[65];   // 40-char git commit SHA
    char checksum[70]; // 64-char sha256
} LockedPackage;

// Full lockfile state
typedef struct {
    int version;
    LockedPackage packages[MAX_DEPS];
    int package_count;
} Lockfile;

// Registry Index Entry
typedef struct {
    char name[MAX_NAME_LEN];
    char version[MAX_VER_LEN];
    DepType type;
    char url[MAX_PATH_LEN];
    char tag[MAX_VER_LEN];
    char description[128];
} RegistryEntry;

// Vulnerability record for audit
typedef struct {
    char package_name[MAX_NAME_LEN];
    char affected_constraint[MAX_VER_LEN]; // e.g. "< 8.4.0"
    char cve_id[32];                      // e.g. "CVE-2023-38545"
    char severity[16];                    // e.g. "HIGH", "CRITICAL"
    char description[128];
} Vulnerability;

// CLI commands
int cmd_init(const char *project_name);
int cmd_add(const char *dep_name, DepType type, const char *url, const char *tag);
int cmd_install(void);
int cmd_build(void);
int cmd_test(void);
int cmd_search(const char *query);
int cmd_sync(void);
int cmd_audit(int generate_sbom);
int cmd_clean(int all);
int cmd_update(void);

// Registry and Index lookup (data-driven)
int registry_init(void);
int registry_load_dir(const char *dir_path);
const RegistryEntry *registry_find(const char *name);
int registry_save_entry(const RegistryEntry *entry);

// Remote tag resolution & layout detection
int find_latest_tag(const char *git_url, char *out_tag, size_t out_sz);
Layout detect_layout(const char *dir);

// SemVer parsing and evaluation
int semver_parse(const char *str, SemVer *out);
int semver_compare(const SemVer *a, const SemVer *b);
void semver_to_string(const SemVer *v, char *buf, size_t sz);
int semver_satisfies(const char *constraint, const char *version_str);

// Manifest parsing and saving
int manifest_read(const char *filename, ProjectConfig *config);
int manifest_write(const char *filename, const ProjectConfig *config);

// Lockfile management (for reproducible builds)
int lockfile_read(const char *filename, Lockfile *lock);
int lockfile_write(const char *filename, const Lockfile *lock);
const LockedPackage *lockfile_find(const Lockfile *lock, const char *pkg_name);
int get_git_commit(const char *dep_dir, char *out_commit, size_t sz);
int get_file_sha256(const char *filepath, char *out_sha, size_t sz);
int get_pkg_version(const char *pkg_name, char *out_ver, size_t sz);

// Helper functions for compilers and libraries
int detect_compiler(char *out_compiler, size_t max_len);
int check_pkg_config(const char *dep_name, char *cflags, size_t cflags_sz, char *libs, size_t libs_sz);
void trim_whitespace(char *str);

#endif // CPM_H
