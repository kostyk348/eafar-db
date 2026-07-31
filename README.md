# EAFAR-DB

**Field-oriented event-driven database** — the first instantiation of the
[EAFAR paradigm](https://github.com/kostyk348/eafar) applied to storage.

The paradigm, in DB terms:

1. **Everything is a field.** A table is a set of named fields (columns),
   stored structure-of-arrays (SoA) — one contiguous array per column. A
   "row" is an index across those arrays; there is no per-row object.
2. **Update only what changed.** Storage is sparse: only inserted keys
   occupy rows; absent keys cost nothing. Pages materialize on first write
   (S2).
3. **Behavior = automata + events + fuzziness.** Materialized views are
   automata with incremental recompute, transactions are snapshots,
   the journal is deterministic replay (S3–S8).

## Status

**v1 — paradigm proof, in progress** (scenario-by-scenario, TDD):

- [x] S1 Columnar table storage — SoA fields (Int64/Float64), CRUD by key,
      NotFound semantics, bit-exact NaN/-0.0 passthrough, swap-with-last
      erase (no tombstones), sparse-friendliness (100M key span, 100k rows)
- [ ] S2 Sparse pages
- [ ] S3 Journal + determinism
- [ ] S4 Transactions = snapshot/rollback (COW)
- [ ] S5 Materialized views as automata
- [ ] S6 View dependency chains
- [ ] S7 Fuzzy queries
- [ ] S8 Replay across transactions

## Query language (v2)

EAFAR-DB's native interface is a custom language, **not SQL** — SQL is the
syntax of relational algebra, and this system is not a relational system.
The language must express what the paradigm is:

```
READ temperature FROM sensors WHERE temperature IS hot   -- fuzzy predicate
READ temperature FROM sensors ON CHANGE                   -- change is default
VIEW avg_temp = AVG(temperature) FROM sensors             -- view = automaton
WATCH fire_events IN sensors                              -- events first-class
```

SQL may arrive later as an optional interop shim, never as the native
interface.

## Build

Requires C++23 (GCC 13+/Clang 16+/MSVC 17.6+), CMake ≥ 3.20. Pulls the
`eafar` core automatically (local checkout preferred, GitHub fallback).

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Or on Windows with MinGW: `./build.ps1`

## License

MIT (pending).
