# SCADA Historian Time-Travel Demo

Shows EAFAR-DB as a plant historian + alarm engine. Run after building
with `-DEAFARDB_DEMOS=ON`:

```
cd build
cmake .. -DEAFARDB_DEMOS=ON
cmake --build . --target scada_time_travel
./scada_time_travel
```

### What the demo shows (SCADA plant)

- **Ingestion**: 10 Modbus-style sensor polls (temp1, temp2, pressure,
  valve), each as an atomic transaction with a wall-clock timestamp.
- **Rollback**: tick 7 arrives with a known sensor glitch (9999°C).
  Operator detects it _before_ commit — transaction is aborted, never
  enters the historian. The journal contains no trace of tick 7.
- **Time-travel** (`replay_at`): state at any past tick is reconstructed
  from the journal alone. `state_before_glitch == row_count=7`: tick 7
  never existed.
- **Views** (S5 + S6 lazy chain):
  - `sum_temp` (S5 materialized SUM view over temp1)
  - `energy_kw` (S6 DerivedView, proportional to sum_temp)
  - `alarm_state` (S6 DerivedView, critical when energy > 50 kW)
  Chain propagates lazily — only the path from source to queried view
  is recomputed, proportionally to dirty pages.
- **Fuzzy trend** (S7): `FuzzyQuery` with `ramp(200,300)` membership
  identifies "hot" rows — membership is proportional, not binary threshold.
- **Audit** (S8): every committed transaction enumerated with its entries
  and timestamps, in commit order.

### Architecture

```
Modbus poll ──► begin_tx ──► set_f/set_i ──► commit ──► journal.ts stamped
                                                            │
                                              replay_at(t) ──► state at t
                                                   │        │
                                                    └────────┘
                                              (lazy view chain propagates
                                               only dirty pages on query)
```

### Key invariants demonstrated

- Journal = primary source of truth (S3)
- Rollback = uncommitted tail truncated (S4)
- Views = automata with incremental recompute (S5, S6)
- Fuzzy = query filter over named membership functions (S7)
- Audit = per-tx enumeration, time-stamped (S8, S9)
- Determinism preserved when timestamps are injected (S9)
