# EAFAR-DB

[![C++20](https://img.shields.io/badge/language-C%2B%2B20-blue)]()
[![Build](https://img.shields.io/badge/build-MinGW--w64-green)]()
[![Tests](https://img.shields.io/badge/tests-142%2F142-brightgreen)]()
[![License](https://img.shields.io/badge/license-Apache--2.0-red)]()
[![Version](https://img.shields.io/badge/version-v0.1.0-blue)]()

> **EAFAR-DB** — Database built on the **EAFAR** design principles *(Everything is a Field, Archive, Replay)*. Embedded, sparse, time-travelable.

## About

EAFAR-DB applies the EAFAR paradigm to persistent storage:

- **E**verything is a **F**ield → column-oriented SoA layout, zero row overhead
- **A**rchive → immutable journal with injectable clock; time-travel queries via `replay_at()`
- **R**eplay → reconstruct any point-in-time state; audit trail as first-class citizen

The database is built from three layers, each independently testable and composable:

| Layer | What it does | EAFAR-DB mechanism |
|---|---|---|
| **Fields** | Data as columns, not rows | `Table<T>` stores each field contiguously |
| **Sparse archive** | Pages only materialize when probed | `Page::mtr()` tracks dirty sub-ranges; unvisited cells cost O(1) |
| **Behavior layer** | Queries, views, transactions as state machines | `View` (automaton), `Event`, `FuzzyQuery` (membership), `Journal` (replay) |

### Quick links

- **Build:** CMake + MinGW-w64, C++20 — see [Getting Started](#build)
- **Demos:** SCADA historian (time-travel queries) + Galaxy Architect (sparse procedural galaxy, turn-based strategy)
- **Spec:** Full S1–S9 design with anti-Goodhart guards in [`spec/eafar_db_spec.md`](spec/eafar_db_spec.md)
- **Topics:** `eafar` · `database` · `sparse-storage` · `soa` · `embedded` · `time-travel` · `incremental-view` · `fuzzy-query` · `automata` · `cxx20` · `minimal-recompute` · `columnar` · `archive` · `replay` · `sparse-pages`

### Design principles (anti-Goodhart)

Every spec scenario has an explicit guard against metric gaming: sparse costs stay O(0) even when fully populated; lazy recompute preserves precision on re-derive; dependency cycles are rejected at declaration; audit enumerates only committed events; materialized view recompute is bounded by delta.

| Principle | Meaning | EAFAR-DB mechanism |
|---|---|---|
| **Everything is a field** (SoA) | Data stored in column-oriented pages, not row-oriented | `Table<T>` stores fields of `T` in contiguous buffers |
| **Update only what changed** | Sparse zero-copy pages avoid writing unmodified data | `Page::mtr()` tracks dirty sub-ranges; only patched bytes serialize |
| **Behavior = automata + events + fuzziness** | Queries and views are state machines with uncertainty | `View` (automaton), `Event`, `FuzzyQuery` (membership predicates) |

## What's in the box

### Core (S1–S4)
- **EAFAR** — C header-only library: fields, sparse pages, hierarchical automata, events, diffusion.
- **EAFAR-DB** — Database layer built on EAFAR: tables with physical pages, materialized views, dependency graphs.

### Feature modules (S5–S9)
| Module | Spec | Status |
|---|---|---|
| **S5** Lazy view automata | Per-page partial recompute, incremental materialization | ✅ |
| **S6** Dependency graph | DAG over derived views, cycle/undeclared-edge rejection | ✅ |
| **S7** Fuzzy queries | Step/ramp/triangle membership predicates via `FuzzyRegistry` | ✅ |
| **S8** Transactions | `BeginTx` / `CommitTx` / roll-forward audit enumeration | ✅ |
| **S9** Journal timestamps + replay | Injectable `Clock`, `replay_at(journal, t)` time-travel historian | ✅ |

### Demos
| Demo | What it shows | Key EAFAR feature |
|---|---|---|
| **SCADA historian** (`demos/scada_time_travel/`) | Industrial telemetry with time-travel queries | S8+S9 transactions & replay |
| **Galaxy Architect** (`demos/galaxy_architect/`) | Sparse procedural galaxy, turn-based strategy | S2 (sparse), S6 (faction automata), S7 (diplomacy), S9 (replay) |
| **Galaxy Architect Phase 2** | Faction AI automata (EXPLORE/EXPAND/WAR/RETREAT), fleet war, fog of war | S6 automata + S8 battle transactions |

## Build

```bash
# Configure (MinGW required — set toolchain path)
cmake -B build -G "MinGW Makefiles" -DCMAKE_CXX_STANDARD=20

# Build all targets (library + tests + demos)
cmake --build build

# Or build a specific target:
cmake --build build --target eafardb_tests
cmake --build build --target galaxy_architect
cmake --build build --target scada_time_travel
```

**Toolchain:** MSYS2/WinLibs MinGW-w64 (GCC 13+, C++20). Set `PATH` to include the MinGW `bin/` directory before building.

## Test status

```
S1–S4 core:  78/78 ✅
EAFAR-DB:    64/64 ✅  (S1–S9, last run: see build output)
```

## Project structure

```
eafar-db/
├── CMakeLists.txt              # Top-level: EAFAR library, eafardb (DB), tests, demos
├── README.md                   # ← You are here
├── include/
│   └── eafardb/
│       ├── table.hpp           # S4: physical-page table implementation
│       ├── view.hpp            # S5: lazy view automaton
│       ├── dependency_graph.hpp # S6: DAG over derived views
│       ├── query.hpp           # S7: fuzzy query interface
│       └── journal.hpp         # S8+S9: transactions + timestamps + replay
├── src/                        # EAFAR-DB implementation files
├── tests/                      # GoogleTest unit tests (S1–S9 coverage)
├── demos/
│   ├── scada_time_travel/      # SCADA historian demo
│   └── galaxy_architect/       # Sparse procedural galaxy game
├── spec/
│   └── eafar_db_spec.md        # Full spec (S1–S9) with anti-Goodhart design
└── .gitignore
```

## Tags

`eafar` `database` `sparse-storage` `soa` `embedded` `time-travel` `incremental-view` `fuzzy-query` `automata` `cxx20` `minimally-recompute`

## License

Apache 2.0 — see LICENSE file.

## Design principles (anti-Goodhart)

Every EAFAR-DB spec scenario has an explicit **anti-Goodhart guard** — a constraint that prevents the implementation from gaming the metric:

- Sparse pages must stay sparse even when fully populated (cost of zero remains O(0) not O(n))
- Lazy view recompute must not silently discard precision on re-derive
- Dependency graph must reject cycles at declaration time, not at query time
- Transaction audit must enumerate exactly-committed events, not "best effort" approximations
- Materialized view recompute must be bounded by delta, not full recompute

## Roadmap

- [x] S1–S4 core pipeline (fields, sparse pages, automata, events)
- [x] S5–S8 feature modules (views, dependencies, fuzzy queries, transactions)
- [x] S9 journal timestamps + time-travel replay
- [x] SCADA historian demo
- [x] Galaxy Architect Phase 1 (sparse galaxy + turn play)
- [ ] Galaxy Architect Phase 2 (faction AI automata + war/fog of war)
- [ ] Galaxy Architect Phase 3 (hierarchical automata per faction)
- [ ] Galaxy Architect Phase 4 (fuzzy diplomacy + fuzzy war)
- [ ] Fuzz testing integration
- [ ] Persistence layer (mmap-based page store)
