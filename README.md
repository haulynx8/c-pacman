# cpm (C Package Manager)

A Cargo/npm-like package manager and build runner for modern C projects.

I started this project because after using `cargo` in Rust and `npm` in web development, going back to writing C in my systems programming class felt super tedious. Every time we need to use external libraries like `cJSON`, `stb_image`, `libcurl`, or `openssl`, half the class spends hours wrestling with compiler includes, missing `.o` object files, or linker errors.

According to the JetBrains 2025 developer survey, **51% of C developers don't use any dependency manager at all**. `cpm` brings the modern `init -> sync -> search -> add -> install -> update -> build -> test -> audit` workflow directly to C.

---

## Complete CLI Feature Set

- **`cpm init [name]`**: Creates a starter project with `cpm.toml`, `src/main.c`, `include/`, and `tests/`.
- **`cpm sync`**: Synchronizes the local package catalog (`~/.cpm/registry/`) from individual package data files or Git repository.
- **`cpm search [query]`**: Searches the package index for libraries matching a name or description.
- **`cpm add <pkg>`**:
  - **Zero-config GitHub resolution**: `cpm add owner/repo` (e.g. `cpm add DaveGamble/cJSON`) automatically queries remote tags with `git ls-remote`, selects the newest SemVer release tag, and caches the package locally.
  - **Registry auto-lookup**: `cpm add cJSON` or `cpm add stb_image` automatically resolves the right URL, Git repo, and tags without having to look them up manually.
  - **Custom Git repositories**: `cpm add mylib --git https://github.com/user/mylib.git --tag v1.0.0`
  - **Direct single-header URLs**: `cpm add custom_h --url https://example.com/custom.h`
  - **System libraries**: `cpm add libcurl` (auto-detected via `pkg-config`).
- **`cpm install`**: 
  - Clones Git repos, downloads headers via `curl`, and pre-compiles library `.c` files into `.cpm/obj/*.o`.
  - **Package Layout Detection (`detect_layout`)**: Automatically detects Plain C source, CMake projects (`cmake --build`), Makefiles (`make`), or single-header libraries.
  - **Full SemVer 2.0.0 Evaluation (`semver.c`)**: Validates version constraints like `^8.0`, `~1.2.0`, or `>= 1.0.0, < 2.0.0`.
  - **Reproducible builds with `cpm.lock`**: Locks Git dependencies to exact 40-character commit SHAs, verifies SHA-256 checksums on downloaded headers, and locks system library versions.
- **`cpm update`**: Re-fetches upstream repositories/headers and refreshes `cpm.lock` with latest compatible versions.
- **`cpm audit [--sbom]`**: Scans dependencies against known vulnerability advisories (CVEs) and exports a standard CycloneDX 1.5 SBOM JSON file.
- **`cpm clean [--all]`**: Cleans build artifacts (`bin/`, `.cpm/obj/`). Adding `--all` also removes cached dependency source trees.
- **`cpm build`**: Auto-detects GCC or Clang and compiles the project with the correct standard (`c17`), headers (`-I.cpm/deps/...`), object files, and linker flags.
- **`cpm test`**: Compiles and runs test suites in `tests/`.

---

## Data-Driven Registry & Zero-Config GitHub Resolution

### 1. Data-Driven Index (`registry/*.toml`)
Packages are not hardcoded inside the C source code. Each package is a standalone TOML file in `registry/` (synced to `~/.cpm/registry/`):

```toml
# registry/cjson.toml
[package]
name = "cJSON"
description = "Ultralightweight JSON parser in ANSI C"
type = "git"
source = "https://github.com/DaveGamble/cJSON.git"
tag = "v1.7.18"
```

Adding a new package to `cpm` is as simple as adding a text file—no recompilation needed.

### 2. Zero-Registry GitHub Adding (`cpm add owner/repo`)
You can add any C repository directly from GitHub without it needing to exist in the registry beforehand:

```bash
$ cpm add DaveGamble/cJSON
[cpm] Detected GitHub repository shorthand: DaveGamble/cJSON
[cpm] Querying remote release tags via git ls-remote...
[cpm] Selected newest release tag: v1.7.19
[cpm] Added Git dependency: cJSON -> https://github.com/DaveGamble/cJSON.git
[cpm] Resolving dependencies for my_project...
  -> [git] Cloning cJSON from https://github.com/DaveGamble/cJSON.git...
  -> [git] cJSON (v1.7.19) checked out
  -> [detect] cJSON has C source files. Compiling objects...
[cpm] Updated cpm.lock (1 package locked)
[cpm] Ready! 1/1 dependencies configured in .cpm/
```

`cpm` automatically queries the remote repository's tags using `git ls-remote`, evaluates them with the built-in SemVer engine, selects the highest stable release, and auto-saves the package into `~/.cpm/registry/` for future offline lookups!

---

## Package Layout Auto-Detection (`detect_layout`)

When cloning a repository, `cpm` automatically inspects the folder structure:
1. **Plain C Libraries** (`count_files_with_ext(".c") > 0`): Compiles library `.c` files directly to `.cpm/obj/*.o` (skipping test runners and `main()` functions).
2. **CMake Projects** (`CMakeLists.txt`): Runs `cmake -S . -B build -DBUILD_SHARED_LIBS=OFF && cmake --build build` and links generated `.a` archives.
3. **Makefile Projects** (`Makefile`): Runs `make` and links static libraries.
4. **Header-only Libraries** (`.h` files only): Automatically wires `-I.cpm/deps/<pkg>` into the build command.

---

## Security & Supply Chain (`cpm audit`)

```bash
# Scan dependencies against known CVEs
$ cpm audit

Auditing dependencies for 'my_server' against known vulnerability advisories...

  [OK] libcurl (8.23.0) - No known vulnerabilities
  [OK] cJSON (v1.7.19) - No known vulnerabilities

Audit Summary: 0 vulnerabilities detected across 2 dependencies.
Tip: Generate an SBOM with: cpm audit --sbom
```

Export a formal CycloneDX SBOM:
```bash
$ cpm audit --sbom
[cpm] Exported CycloneDX SBOM to: cpm.sbom.json
```

---

## Semantic Versioning (`semver.c`)

`cpm` implements a native SemVer 2.0.0 parser and comparator supporting:
- **Caret ranges (`^`)**: `^1.2.3` allows `>= 1.2.3, < 2.0.0` (prevents breaking changes). Respects `0.x` breaking rules (`^0.2.3` rejects `0.3.0`).
- **Tilde ranges (`~`)**: `~1.2.0` allows patch-level updates (`< 1.3.0`).
- **Comparison operators**: `>=`, `<=`, `>`, `<`, `=`.
- **Compound constraints**: e.g. `libcurl = ">= 8.0.0, < 9.0.0"`.
- **Prerelease & build metadata**: Correct precedence handling (`1.0.0 > 1.0.0-alpha`, build metadata ignored per spec).

Run the SemVer test suite:
```bash
cpm test
```

---

## Quickstart

```bash
# 1. Initialize a new project
cpm init my_server
cd my_server

# 2. Sync local package registry
cpm sync

# 3. Add dependencies (from registry, GitHub, or system)
cpm add DaveGamble/cJSON     # GitHub owner/repo shorthand (auto-resolves newest tag)
cpm add libcurl              # System pkg-config
cpm add stb_image            # Single-header registry entry

# 4. Build and run!
cpm build
./bin/my_server

# 5. Check security & SBOM
cpm audit --sbom
```

---

## Build cpm from source

```bash
cd Projects/cpm
make
./cpm test
```
