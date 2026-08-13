# 11. CI, artifacts, and release strategy

## Goals

1. Every change on `master` / PR is **built and tested** automatically.
2. CI produces **downloadable artifacts** (binaries + example map).
3. Releases follow **SemVer** and publish signed-by-workflow checksums on GitHub Releases.

## CI pipeline (`.github/workflows/ci.yml`)

| Trigger | Action |
|---------|--------|
| `push` to `master` | build + test (GCC, Clang) |
| `pull_request` → `master` | build + test |
| `workflow_dispatch` | manual run |

### Jobs

- **Ubuntu 24.04 + g++** (primary): CMake preset `ci`, `ctest`, package `opc-server-linux-x64.tar.gz` + SHA256 → **Actions artifact** (14 days).
- **ASan + UBSan**: preset `asan` (GCC), full Catch2 suite.
- **TSan**: preset `tsan` (GCC) on core/runtime tests; UA client smoke is skipped because open62541 is not TSan-instrumented.
- **Conan 2**: optional dependency provider job (`OPC_DEPENDENCY_PROVIDER=CONAN`).
- Clang is supported locally with `-DCMAKE_CXX_FLAGS=-stdlib=libstdc++` when libstdc++ provides `std::expected`; not required in CI until runners ship a complete C++23 STL for Clang.
- **Studio quality**: npm lockfile install, lint, TypeScript check, Vitest and
  production web-assets build.
- **Studio package matrix**: native `opc-map`/`opc-monitor` sidecars plus Tauri
  packages for Windows x64, Linux x64 and macOS arm64. Packages are retained as
  Actions artifacts for 14 days.

Artifacts from CI are **not** GitHub Releases — they are intermediate build products for debugging/QA.

## Release strategy

### Versioning (SemVer)

| Tag | Meaning |
|-----|---------|
| `vMAJOR.MINOR.PATCH` | Stable release (e.g. `v0.1.0`) |
| `vMAJOR.MINOR.PATCH-rc.N` | Release candidate / pre-release |
| `vMAJOR.MINOR.PATCH-beta.N` | Pre-release |

Rules:

- **MAJOR** — breaking changes to `*.modbusproj.json` schema or public CLI without migration path.
- **MINOR** — new features, backward-compatible.
- **PATCH** — fixes, docs, CI.
- Pre-release suffixes (`-rc`, `-beta`, `-alpha`) → GitHub Release marked **prerelease**.

`project(OPC_SERVER VERSION …)` in CMake tracks the same SemVer **without** the leading `v`.

### How to cut a release

```bash
# 1. master is green on CI
git checkout master
git pull origin master

# 2. Update CHANGELOG.md (Unreleased → version section)

# 3. Tag and push
git tag -a v0.2.0 -m "OPC_SERVER v0.2.0"
git push origin v0.2.0
```

Pushing the tag runs `.github/workflows/release.yml`:

1. Build Release + tests
2. Package `opc-server-<version>-linux-x64.tar.gz` + `.sha256`
3. Upload Actions artifact
4. Create/update **GitHub Release** with notes + assets

Manual: Actions → **Release** → Run workflow → enter existing tag.

### What ships in a release tarball

```text
opc-server-<ver>-linux-x64/
  bin/OPC_SERVER
  bin/opc-map
  bin/opc-monitor
  examples/demo-plant.modbusproj.json
  examples/modbus-project.schema.json
  README.md
  CONTRIBUTING.md
```

Engineering Studio is produced independently as native Tauri bundles (`msi`/
`nsis`, `deb`/`AppImage`, `app`/`dmg`). Signing and Apple notarization are
release gates that require repository secrets; unsigned CI bundles are for QA.

### Support policy (v0.x)

- `v0.x` — API/schema may change with MINOR bumps; document migrations in CHANGELOG.
- Security fixes preferred on latest MINOR.
- No LTS until `v1.0.0`.

## GitHub settings (manual checklist)

Repository admin should confirm:

1. **Actions** enabled (Allow GitHub Actions).
2. Workflow permissions: Read and write (needed for `release.yml` to publish Releases via `GITHUB_TOKEN`).
3. **Actions → General → Artifact retention** — default OK (CI uses 14d, release workflow 30d).
4. Branch protection on `master` (recommended):
   - Require status check `Build & test (ubuntu-24.04 / g++)`
   - Require PR before merge (optional for solo maintainers)
5. Releases: allow auto notes (workflow uses `generate_release_notes: true`).

## Local package smoke

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix stage
./stage/bin/opc-map validate DOCs/examples/demo-plant.modbusproj.json
```
