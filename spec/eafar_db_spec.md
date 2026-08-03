# EAFAR-DB Specification (S1–S9)

> Anti-Goodhart technical specification. Every scenario below has an explicit
> **guard** — a constraint that prevents an implementation from "gaming the
> metric" (e.g. claiming sparse cost O(0) while actually scanning the whole
> key range). Scenarios are graded: `excellent` = the ideal behavior the
> guard exists to protect; `good` = acceptable; `bad` = guard violated.
>
> Register: this document is the **spec** (contract), not the implementation.
> The implementation lives in `include/eafardb/*.hpp` + `src/*.cpp`; the
> current status of each scenario is tracked in `README.md → Feature modules`.

---

## S1 — Everything is a field (columnar SoA storage)

**Thesis.** A table is a set of named fields, stored column-oriented
(SoA): one contiguous array per column *within each page*. A "row" is an
index across the page's arrays; there is no per-row object. Zero row
overhead.

**Constraint C1 (hard).** No per-row allocation, no per-row object, no row
header. Storage must be block-partitioned columnar: `row_keys` + per-type
column arrays per page (`PageData::int_cols`, `PageData::float_cols`).

**Scenario S1.1 — columns are independent arrays.**
- Expected: `add_column("temp", FLOAT)` returns a column id; `set_f(key,
  col, v)` writes into the float array of the page holding `key`; reading
  back `get_f` returns the same value.
- Grade: excellent — all rows of a column are contiguous per page, no row
  header exists; good — columnar layout exists but with some per-row
  indirection; bad — row-oriented storage or per-row objects.

**Scenario S1.2 — mixed types stay separate.**
- Expected: int and float columns coexist; `get_i` on a float column
  throws (type mismatch is a hard error, not silent reinterpretation).
- Grade: excellent — type-checked access with explicit error; bad —
  silent cast between int and float columns.

**Guard G1.** The anti-Goodhart metric is "zero row overhead". A
row-oriented implementation must not pass S1.1/S1.2; a columnar one cannot
cheat by hiding rows in per-row objects.

---

## S2 — Sparse pages (O(0) for untouched keys)

**Thesis.** Storage is physically sparse: a page exists only if it holds at
least one row. A sleeping page is zero memory. Waking happens on first
write. Scan cost is proportional to materialized pages, never to the key
range.

**Constraint C2 (hard).** Pages outside the materialized set cost O(0):
`has_page(pid) == false` and no allocation for them. Scan cost must be
`O(#materialized pages)`, not `O(key range)`.

**Scenario S2.1 — untouched key range costs nothing.**
- Expected: with `page_size = 256`, `insert(0)` materializes exactly one
  page; `insert(10_000_000)` materializes one more; no page exists for
  any other range.
- Grade: excellent — `page_count()` grows only when a new range is first
  written; good — pages exist but are lazily zeroed; bad — a full-key-space
  array or bitmap is allocated at table creation.

**Scenario S2.2 — scan touches each distinct page once.**
- Expected: after writing keys `0, 1, 2, 3, 4` (one page), a full scan
  visits exactly 1 page: `touched_pages()` delta == 1.
- Grade: excellent — page-visit counter increments once per distinct page;
  bad — counter counts keys or rows instead of pages (metric gaming: the
  "cost" number would scale with data, not with materialization).

**Scenario S2.3 — negative keys map via uint64.**
- Expected: `page_id(-1) != page_id(0)`; negative keys live in their own
  pages; scans in ascending key order remain deterministic.
- Grade: excellent — negative keys fully supported with no ambiguity;
  bad — negatives collide with positives or are rejected.

**Guard G2.** The metric "sparse cost O(0)" must stay true even when the
table is fully populated (S2.2's counter must scale with pages, not rows).
If a page is materialized, its *storage* is O(rows in page) — but untouched
pages must never be paid for.

---

## S3 — Journal: primary source of truth

**Thesis.** The write journal is the primary source of truth; the table
state is derived from it. Replaying the journal from an empty table
reproduces state bit-exactly — including NaN bit patterns and -0.0.

**Constraint C3 (hard).** Every mutation is recorded in the journal in
apply order: schema ops (`add_column`) and data ops (`insert`, `erase`,
`set_i`, `set_f`). Float values are stored bit-exact (`value_bits`,
`memcpy` of double), never sanitized.

**Scenario S3.1 — replay is bit-identical.**
- Expected: mutate a table (insert, erase, set float to NaN and to -0.0),
  then `Table::replay(journal)` == the source table: same rows, same bits.
- Grade: excellent — `replay` reproduces NaN bit patterns and -0.0 exactly;
  good — values equal under `==` but sign/NaN payloads lost; bad — replay
  differs from source or drops schema ops.

**Scenario S3.2 — journal is self-contained.**
- Expected: replay from a *fresh* `Table::replay(journal)` with no schema
  knowledge reproduces schema + data (schema ops are replayed too).
- Grade: excellent — schema and data come from the journal alone;
  bad — replay requires external schema setup.

**Guard G3.** The metric is "replay fidelity". Round-tripping a value
through `double` and back must not lose bits; an implementation that
normalizes floats (e.g. -0.0 → +0.0) fails S3.1 even though all
comparisons pass.

---

## S4 — Transactions = snapshot/rollback (COW)

**Thesis.** `begin_transaction()` takes a cheap snapshot (page-map copy of
shared page buffers — no data copied). Writes to a shared page deep-copy it
on first touch (copy-on-write): cost proportional to the dirty page, never
the table. `rollback()` restores the snapshot bit-exactly and truncates
uncommitted journal entries.

**Constraint C4 (hard).** Transaction begin must be O(#pages) pointers, not
O(#cells). Copy-on-write must copy only the dirty page. Rollback must
restore state bit-exactly and remove uncommitted journal entries.

**Scenario S4.1 — begin is cheap, commit copies only dirty pages.**
- Expected: begin → N writes to a *single* page → commit; `copied_cells()`
  equals that page's cell count (one page copied), not the whole table.
- Grade: excellent — COW counter proves per-dirty-page cost; good — copies
  happen but counter is absent; bad — snapshot deep-copies the whole table.

**Scenario S4.2 — rollback is bit-exact and journal is clean.**
- Expected: begin → insert/set → rollback → table equals pre-begin state;
  journal `size()` equals pre-begin size (no `BeginTx`/`CommitTx` residue).
- Grade: excellent — bit-exact restore + journal truncation;
  bad — rollback leaves journal entries behind.

**Guard G4.** The metric "cost proportional to dirty page" is proven by
`copied_cells_`. An implementation that copies everything on begin fails
the counter check; one that skips COW and shares pages mutably corrupts
S4.2.

---

## S5 — Materialized view as automaton

**Thesis.** A view is an automaton: events = page-touching writes on the
source; state = per-page partials + cached global; step = recompute ONLY
the pages that received events (lazy, on query). Work is proportional to
dirty pages, never to the table.

**Constraint C5 (hard).** After full materialization, one write to one
page must recompute one page partial — not the whole column, not the whole
table. Recompute must be lazy (nothing until queried).

**Scenario S5.1 — incremental materialization.**
- Expected: `View(source, float_col)`; query → full materialization (all
  pages). Then write one key on one page, query again →
  `recomputed_pages()` grows by 1, `recomputed_rows()` grows by ≤ page
  rows, not by all rows.
- Grade: excellent — dirty-only recompute with counters proving it;
  good — recompute happens but counters are absent; bad — any single-key
  write triggers a full re-scan of the column.

**Scenario S5.2 — laziness.**
- Expected: after construction with no query, writes mark pages dirty but
  `recomputed_pages() == 0`; first query materializes.
- Grade: excellent — no work before first query; bad — constructor
  materializes eagerly.

**Guard G5.** The metric is "recompute bounded by delta". The counters
`recomputed_pages_` / `recomputed_rows_` must scale with dirty pages, not
table size. A full-recompute-on-every-query implementation fails S5.1.

---

## S6 — Dependency graph (views on views)

**Thesis.** A `DerivedView` declares its inputs via a `DependencyGraph`
and combines them lazily: it recomputes only when an input's version
actually grew. Declared edges only; cycles rejected at declaration; lazy
propagation.

**Constraint C6 (hard).** Cycle rejection must happen at declaration time,
not at query time. Undeclared nodes/inputs are rejected. Downstream views
recompute only when an upstream version changed.

**Scenario S6.1 — cycle rejected at declaration.**
- Expected: `A -> B -> A` throws at `DerivedView` construction.
- Grade: excellent — constructor throws on cycle; bad — cycle detected
  only at query time (or never, → infinite recursion / stale cache).

**Scenario S6.2 — undeclared input rejected.**
- Expected: constructing a view whose input is not in the graph throws.
- Grade: excellent — undeclared edge rejected immediately;
  bad — undeclared inputs silently accepted.

**Scenario S6.3 — lazy propagation by version.**
- Expected: two views `A -> C`, `B -> C`; only `A`'s source changes;
  querying `C` recomputes once (C's `recomputes()` grows by 1, not 2) and
  B is not materialized.
- Grade: excellent — version-based skip: no recompute when nothing grew;
  bad — C always recomputes on any query.

**Guard G6.** The metric is "recompute only on real change". Comparing
input *versions* (monotonic recompute counters) must gate recompute; an
implementation that recomputes on any query (even with unchanged inputs)
fails S6.3.

---

## S7 — Fuzzy queries (fuzziness in the DB)

**Thesis.** A fuzzy query filters rows where
`membership(field, concept) >= threshold`. Membership functions are named
entries in the core `FuzzyRegistry`, swapped at runtime without touching
query code (automata depend on names, not function shapes).

**Constraint C7 (medium).** Fuzzy is query-time only: no storage changes,
no new index. The filter walks the source column (SoA scan) and applies the
registry. Unknown concept names throw `std::out_of_range` (hard error, not
empty result).

**Scenario S7.1 — registry-driven filtering.**
- Expected: define "hot" membership; `FuzzyQuery::filter(table, col, "hot",
  0.5)` returns keys in ascending order where membership ≥ 0.5.
- Grade: excellent — keys ascending, threshold inclusive, unknown concept
  throws; bad — unknown concept silently returns empty.

**Scenario S7.2 — swap without touching query code.**
- Expected: replacing the registry's membership function for "hot"
  changes results of an *already constructed* `FuzzyQuery`.
- Grade: excellent — behavior changes via registry alone, query object
  untouched; bad — membership baked into query construction.

**Guard G7.** The metric is "fuzziness by name". The query must depend on
the concept *name*, not the function; a query that hard-codes membership
logic fails S7.2.

---

## S8 — Transactions as journal boundaries (audit)

**Thesis.** `BeginTx` / `CommitTx` are journal boundary markers used for
audit. `transaction_ranges()` enumerates half-open index ranges
`[begin, end)` of every committed transaction, in commit order. Entries
outside any transaction are not part of any range. `truncate(keep)` drops
uncommitted work (an aborted transaction is not history).

**Constraint C8 (hard).** Audit must enumerate exactly-committed events —
not "best effort" approximations. An unclosed `BeginTx` (crash / no
commit) must not appear in `transaction_ranges()`.

**Scenario S8.1 — committed ranges only.**
- Expected: begin; writes; commit; begin; writes (no commit) →
  `transaction_ranges()` has exactly 1 range covering the first tx.
- Grade: excellent — unclosed tx excluded, ranges in commit order;
  bad — unclosed tx included, or order scrambled.

**Scenario S8.2 — truncate removes uncommitted work.**
- Expected: `truncate(begin_index)` drops the open tx's entries; journal
  replay from truncated point yields the pre-tx state.
- Grade: excellent — truncate + replay agree with rollback semantics;
  bad — truncate leaves holes or breaks replay.

**Guard G8.** The metric is "audit enumerates committed events exactly".
`transaction_ranges()` derives from journal markers only; an
implementation that tracks transactions in a side structure (rather than
the journal) risks divergence on replay.

---

## S9 — Journal timestamps + time-travel replay

**Thesis.** Every journal entry is stamped with a monotonic timestamp from
an injectable clock. `replay_at(journal, t)` reconstructs the state as of
timestamp `t`: replays only entries with `ts <= t`. The journal is a
historian.

**Constraint C9 (hard).** The clock is injectable (`set_clock`) and only
affects timestamps of FUTURE entries — history keeps its ts values.
`replay_at` must be deterministic: same journal + same t → same state.

**Scenario S9.1 — deterministic time-travel.**
- Expected: drive a fake clock: insert at t=10, set at t=20, erase at
  t=30. `replay_at(journal, 15)` == state with insert applied, set not
  applied; `replay_at(journal, 25)` == insert + set; `replay_at(journal,
  35)` == empty (erased).
- Grade: excellent — ts <= t filter exact, deterministic across calls;
  good — correct at coarse granularity but clock affects old entries;
  bad — timestamps not recorded, or replay ignores t.

**Scenario S9.2 — clock swap affects only the future.**
- Expected: insert at t=10 with fake clock; swap clock; insert again;
  the first entry still has ts=10.
- Grade: excellent — history immutable after append; bad — entries
  re-stamped on read.

**Guard G9.** The metric is "history is immutable + replay is exact". An
implementation that stamps entries at *read* time (rather than append
time) fails S9.2 and makes replay non-deterministic.

---

## Non-goals (explicitly out of scope)

- **No SQL.** EAFAR-DB is an embedded API, not a SQL engine.
- **No network / no client-server.** Single-process, in-memory (persistence
  layer is a roadmap item, not a spec scenario).
- **No ACID isolation levels.** S4/S8 provide snapshot-rollback +
  commit-audit; no multi-threaded concurrency guarantees.
- **No storage format stability.** Journal/table in-memory layouts are not
  a wire/storage ABI yet.
- **No fuzzy index acceleration.** S7 walks columns; indexing is
  explicitly out of scope.

---

## Anti-Goodhart summary (guard table)

| # | Metric being protected | Guard |
|---|---|---|
| G1 | zero row overhead | S1.1/S1.2 structural checks |
| G2 | sparse cost O(0) | S2.2 counter scales with pages, not rows |
| G3 | replay fidelity | S3.1 bit-exact NaN/-0.0 |
| G4 | per-dirty-page COW cost | S4.1 `copied_cells` counter |
| G5 | recompute bounded by delta | S5.1 counters scale with dirty pages |
| G6 | recompute on real change only | S6.3 version-gated lazy propagation |
| G7 | fuzziness by name | S7.2 registry swap without code change |
| G8 | audit = exactly committed | S8.1 unclosed tx excluded |
| G9 | history immutable + replay exact | S9.2 clock swap affects future only |

*End of EAFAR-DB Specification v1.0 — SINT Core Team.*
