# Contributing to EAFAR-DB

## How to contribute

1. Fork the repository
2. Create a feature branch: `git checkout -b feat/your-feature`
3. Commit with conventional commit style: `feat(scope): description`
4. Push and open a PR

## Commit conventions

We use [Conventional Commits](https://www.conventionalcommits.org/):

| Type | Description |
|---|---|
| `feat` | New feature (demo, module, spec scenario) |
| `fix` | Bug fix |
| `refactor` | Code restructuring, no behavior change |
| `docs` | Documentation / README / spec updates |
| `test` | Test coverage or test-only changes |
| `perf` | Performance improvement |
| `chore` | Build config, CI, tooling |

## Architecture overview

EAFAR-DB is a C++20 embedded database implementing the EAFAR paradigm:

```
┌──────────────────────────────────────────────┐
│              EAFAR-DB (S1–S9)                │
├──────────────┬───────────────────────────────┤
│  EAFAR core  │  Storage layer                │
│  (fields+    │  • Table<T> with SoA layout   │
│   automata)  │  • Sparse pages (mtr())       │
│              │  • Materialized views          │
│              │  • Dependency graph            │
│              │  • Fuzzy queries               │
│              │  • tx audit + journal replay   │
└──────────────┴───────────────────────────────┘
```

## Code style

- C++20, `<namespace>` scoped enums where appropriate
- Header-only for EAFAR core; source `.cpp` for EAFAR-DB
- GoogleTest for tests (`tests/`)
- CMake build system, MinGW-w64 toolchain

## Testing

```bash
cd build
ctest --output-on-failure
```

All 64 EAFAR-DB tests + 78 EAFAR core tests must pass.

## Project status

| Module | Status |
|---|---|
| S1–S4 core engine | ✅ Complete |
| S5–S8 feature modules | ✅ Complete |
| S9 journal + replay | ✅ Complete |
| SCADA historian demo | ✅ Complete |
| Galaxy Architect demo | 🔄 Phase 1 → 4 planned |
| Persistence (mmap) | 📋 Planned |
| Fuzz testing | 📋 Planned |
