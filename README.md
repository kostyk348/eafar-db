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

**v1 — paradigm proof, complete** (8 scenarios + industrial time-travel extension):

- [x] S1 Columnar table storage — SoA fields (Int64/Float64), CRUD by key,
      NotFound, bit-exact NaN/-0.0, swap-with-last erase, sparse-friendliness
- [x] S2 Sparse pages — pages materialize on first write, sleep when empty;
      scan cost proportional to written pages (1M key span / 1 page → 1 touched)
- [x] S3 Journal + determinism — append-only, bit-exact replay incl. NaN/-0.0,
      layout-independent
- [x] S4 Transactions = snapshot/rollback (COW) — rollback bit-exact,
      snapshot cost proportional to dirty pages (counter-proven)
- [x] S5 Materialized views as automata — SUM view, per-page partials,
      lazy incremental recompute (1 write → 1 page recomputed)
- [x] S6 View dependency chains — declared edges, cycle rejection,
      lazy selective propagation (B recomputed only when A changed)
- [x] S7 Fuzzy queries — membership filter (step/ramp/triangle) via core
      FuzzyRegistry; swap the function without touching query code
- [x] S8 Replay across transactions — tx boundaries in journal,
      rollbacks not replayed, per-tx audit enumeration
- [x] S9 *(extension)* Journal timestamps — monotonic ts on every entry,
      injectable clock, `replay_at(t)` time-travel (historian/audit)

**64/64 tests green.**

## Industrial fit

The journal is an append-only, immutable, time-stamped record of every state
change — i.e. an **audit trail with provenance** out of the box. Combined with
`replay_at(t)` (state as of any timestamp) and fuzzy trend predicates, this is
the stack SCADA/IIoT actually needs *above* the field bus:

| SCADA need | EAFAR-DB |
|---|---|
| tag historian | journal = history of every change, no sampling loss |
| time-travel / audit | `replay_at(t)` + `transaction_ranges()` |
| HMI dirty updates | update only what changed (same as GUI dirty regions) |
| alarm automata | views/automata over field events, not imperative code |
| trend detection | fuzzy predicates (`temperature IS rising`) |

The control loop (PLC at 1 kHz) and the protocol stacks (Modbus/OPC-UA) stay
where they are — EAFAR-DB is the **state + historian + audit layer** over them.

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
