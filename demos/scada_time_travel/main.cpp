// demos/scada_time_travel/main.cpp — SCADA historian time-travel demo.
//
// Showcases EAFAR-DB as a plant historian + alarm engine:
//   * Modbus-style sensor feed stored with monotonic timestamps (S9)
//   * SUM view (S5) tracks total thermal energy
//   * Alarm chain (S6): sum_temp → energy_kw → critical_alarm
//   * Fuzzy trend filter (S7): "temperature IS hot"
//   * Time-travel: state at any past moment (replay_at, S9)
//   * Rollback: late-arriving bad reading discarded before commit (S4)
//   * Audit: enumerate every committed transaction (S8)
//
// Demonstrated invariants:
//   journal = historian (every change stamped, every tx auditable)
//   replay_at(t) = state at any past wall-clock moment
//   rollback = transaction not committed (no trace in journal)
//   alarm chain = lazy propagation through dependency graph
//   fuzzy trend = new class of alert (proportional, not threshold)

#include "eafardb/table.hpp"
#include "eafardb/derived_view.hpp"
#include "eafardb/dependency_graph.hpp"
#include "eafardb/query.hpp"

#include <cstdio>
#include <cstdint>
#include <cstring>

using namespace eafardb;
using namespace eafar;
using namespace eafar::mf;

static const char* op_name(JournalOp op) {
    switch (op) {
    case JournalOp::AddColumn: return "AddColumn";
    case JournalOp::Insert:    return "Insert";
    case JournalOp::Erase:     return "Erase";
    case JournalOp::SetI:      return "SetI";
    case JournalOp::SetF:      return "SetF";
    case JournalOp::BeginTx:   return "BeginTx";
    case JournalOp::CommitTx:  return "CommitTx";
    }
    return "?";
}

static void print_audit(const Table& t) {
    const auto& entries = t.journal().entries();
    const auto ranges   = t.journal().transaction_ranges();
    printf("\n═══ Audit (%zu entries, %zu committed tx) ═══\n",
           entries.size(), ranges.size());
    for (const auto& [b, e] : ranges) {
        printf("  TX [%zu..%zu)\n", b, e);
        for (std::size_t i = b; i < e; ++i) {
            const auto& en = entries[i];
            printf("    %s", op_name(en.op));
            if (en.op == JournalOp::SetF || en.op == JournalOp::SetI) {
                printf(" key=%lld col=%u", (long long)en.key, en.column);
            }
            if (en.op == JournalOp::AddColumn) {
                printf(" name=\"%s\"", en.name.c_str());
            }
            printf("  ts=%llu\n", (unsigned long long)en.ts);
        }
    }
}

// ────────────────────────────────────────────────────────────────────
// Simulated chemical plant:
//   sensor 0 = reactor inlet temperature  (°C × 10 → int64 for demo)
//   sensor 1 = reactor outlet temperature
//   sensor 2 = vessel pressure (bar × 10)
//   sensor 3 = valve open(1)/closed(0)
//
// Feed = Modbus poll at 1 s intervals, each poll is a tx.
// Tick 7 arrives with a known sensor glitch (9999°C) —
// operator detects anomaly BEFORE commit → tx rolled back,
// no record in the historian at all (best possible outcome).
// ────────────────────────────────────────────────────────────────────

int main() {
    printf("═══════════════════════════════════════════════════════\n");
    printf("  EAFAR-DB — SCADA historian time-travel demo\n");
    printf("═══════════════════════════════════════════════════════\n\n");

    // ── 1. Schema ──────────────────────────────────────────────
    Table sensors(256);
    sensors.add_column("temp1", ColumnType::Float64);  // reactor inlet
    sensors.add_column("temp2", ColumnType::Float64);  // reactor outlet
    sensors.add_column("press", ColumnType::Float64);  // vessel pressure
    sensors.add_column("valve", ColumnType::Int64);    // 1=open 0=closed

    // ── 2. Views (S5 + S6 chain) ──────────────────────────────
    // A: SUM of temp1 (thermal energy proxy).
    View sum_temp(sensors, 0);

    // Alarm chain: A → B → C (lazy propagation, S6).
    // B = energy_kw ≈ sum_temp * 0.01 (proportional power)
    // C = critical_alarm = 1 when energy_kw > 50 kW
    DependencyGraph g;
    g.declare_node("sum_temp");
    // DerivedViews auto-register themselves and declare their input edges.
    auto  to_kw  = [](const std::vector<double>& v) { return v[0] * 0.01; };
    DerivedView  energy("energy_kw", g, {{"sum_temp", &sum_temp}}, to_kw);
    auto alarm_fn = [](const std::vector<double>& v) -> double {
        return v[0] > 50.0 ? 1.0 : 0.0;   // 1 = 🔴 CRITICAL
    };
    DerivedView alarm_view("alarm_state", g, {{"energy_kw", &energy}}, alarm_fn);

    // ── 3. Fuzzy registry (S7) ─────────────────────────────────
    eafar::FuzzyRegistry reg;
    reg.define("high",     ramp(200.0f, 300.0f));   // reactor hot band
    reg.define("critical", step(280.0f));            // hard safety limit

    // ── 4. Sensor feed ──────────────────────────────────────────
    // Each poll = an atomic tx with a wall-clock timestamp (S9).
    // ts_start = arbitrary "epoch" in ms (Dec 2023 for demo).
    enum : std::uint64_t { ts_start = 1'700'000'000'000ULL };

    struct Reading {
        int64_t tick;   // Modbus register address / poll index
        double  t1, t2, p;
        int64_t valve;
    };

    const Reading feed[] = {
        { 0,  80.0,  81.0,  4.2, 1 },
        { 1,  80.5,  81.5,  4.2, 1 },
        { 2,  81.0,  82.0,  4.3, 1 },
        { 3, 260.0,  82.5,  4.3, 1 },   // ramping toward hot band
        { 4, 290.0,  83.0,  4.3, 1 },   // CRITICAL — ≥ 280 step threshold
        { 5, 295.0,  83.5,  4.4, 1 },
        { 6,  80.0,  82.0,  4.3, 0 },   // valve closes, cooling starts
        // ─── tick 7: bad sensor glitch (operator ABORTS tx) ────
        { 7, 9999.0, 83.0, 4.3, 0 },
        { 8,  80.5,  82.5,  4.2, 0 },   // post-recovery, normal
        { 9,  81.0,  82.0,  4.1, 0 },
    };

    std::printf("\n── Ingesting %zu Modbus polls (each as a tx) ──\n\n",
                sizeof(feed) / sizeof(feed[0]));

    for (std::size_t i = 0; i < sizeof(feed) / sizeof(feed[0]); ++i) {
        const auto& r = feed[i];

        // Install deterministic clock for this poll (S9 timestamps).
        sensors.set_journal_clock([ts = ts_start + r.tick * 1000] {
            return ts;
        });

        sensors.begin_transaction();

        // ── Scenario: tick 7 is a known glitch ──────────────────
        // Operator spots 9999°C on the HMI before the tx is committed.
        // Correct SCADA response: abort (rollback), not commit.
        if (r.t1 > 500.0) {
            sensors.rollback();
            printf("[tick %2lld] ⚠  GLITCH (%.0f°C) — tx rolled back, "
                   "never entered historian\n",
                   (long long)r.tick, r.t1);
            continue;
        }

        sensors.insert(r.tick);   // materialize the row first
        sensors.set_f(r.tick, 0, r.t1);      // temp1
        sensors.set_f(r.tick, 1, r.t2);      // temp2
        sensors.set_f(r.tick, 2, r.p);       // pressure
        sensors.set_i(r.tick, 3, r.valve);   // valve
        sensors.commit();

        // ── Read views lazily (S5 + S6 chain) ───────────────────
        double total  = sum_temp.value();     // S5: incremental recompute
        double kw     = energy.value();       // S6: lazy prop from A
        double alarm  = alarm_view.value();   // S6: lazy prop from B

        // ── Fuzzy query (S7) "hot rows" ──────────────────────────
        FuzzyQuery fq(reg);
        auto hot_keys = fq.filter(sensors, 0, "high", 0.5f);

        const char* alarm_str = alarm > 0.5 ? "🔴 CRITICAL" : "🟢 OK";
        printf("[tick %2lld] t1=%.1f  t2=%.1f  p=%.1f  valve=%lld  "
               "| sum=%.1f  kw=%.2f  alarm=%s  hot_rows=%zu\n",
               (long long)r.tick, r.t1, r.t2, r.p, r.valve,
               total, kw, alarm_str, hot_keys.size());

        // ── Time-travel (S9): state at this exact tick ───────────
        // replay_at(ts) == state at the moment this poll committed.
        const Table at_tick = Table::replay_at(sensors.journal(),
                                               ts_start + r.tick * 1000);
        printf("      ↳ time-travel to tick %lld  →  row_count=%zu  "
               "replay matches current? %s\n",
               (long long)r.tick, at_tick.row_count(),
               at_tick.row_count() == sensors.row_count() ? "yes" : "no");
    }

    // ── 5. Post-rollback time-travel (S9) ───────────────────────
    printf("\n── Time-travel: state 1 s BEFORE the glitch (tick 6) ──\n");
    {
        const Table at_tick6 = Table::replay_at(sensors.journal(),
                                                 ts_start + 6 * 1000);
        printf("tick 6 state → row_count=%zu  "
               "(tick 7 NEVER exists in the journal)\n",
               at_tick6.row_count());
    }

    // ── 6. Audit enumeration (S8) ──────────────────────────────
    print_audit(sensors);

    // ── 7. Trend query demonstration (S7: fuzzy ≠ threshold) ──
    printf("\n── S7: fuzzy trend vs fixed threshold ──\n");
    printf("Rows where temp1 IS 'hot' (ramp 200-300, membership ≥ 0.5):\n");
    {
        FuzzyQuery fq(reg);
        auto hot = fq.filter(sensors, 0, "high", 0.5f);
        for (int64_t k : hot) {
            double v = sensors.get_f(k, 0);
            printf("  tick %-2lld  temp1 = %.1f°C  ",
                   (long long)k, v);
            // show membership value
            float m = reg.apply("high", static_cast<float>(v));
            printf("membership(%.1f..300) = %.2f\n", 200.0, m);
        }
    }

    // ── 8. Summary stats ───────────────────────────────────────
    printf("\n═══ Final state ═══\n");
    printf("rows_in_historian : %zu\n", sensors.row_count());
    printf("committed_tx      : %zu\n",
           sensors.journal().transaction_ranges().size());
    printf("journal_size      : %zu entries\n", sensors.journal().size());
    printf("recomputed_pages  : %llu  (S5: work proportional to dirty pages)\n",
           (unsigned long long)sum_temp.recomputed_pages());
    printf("energy_kw.recomputes : %llu  (S6: lazy chain propagation)\n",
           (unsigned long long)energy.recomputes());
    printf("alarm_state.recomputes: %llu\n",
           (unsigned long long)alarm_view.recomputes());

    printf("\n═══════════════════════════════════════════════════════\n");
    printf("  Demo complete — historian with time-travel built in.\n"
           "  Rollback, audit, fuzzy trend, lazy view chain — all in one.\n");
    printf("═══════════════════════════════════════════════════════\n");

    return 0;
}
