# Lab harnesses

Opt-in tests that are **skipped** in default `ctest` unless an environment flag is set.

| Flag | Catch2 tag | Script |
|------|------------|--------|
| `OPC_E2E=1` | `[e2e]` | `scripts/run-e2e.sh` |
| `OPC_SOAK=1` | `[soak]` | `scripts/soak-runtime.sh` |

Benchmarks: `./build/dev/tests/opc_bench` (not registered in CTest).

Fuzzer (Clang): `cmake --preset dev -DOPC_ENABLE_FUZZERS=ON` then `./build/dev/fuzz/project_load_fuzzer`.

FAT/SAT/CTT checklists: `DOCs/testing/`.
