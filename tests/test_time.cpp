// eafardb/tests/test_time.cpp — S9 contract tests (extension).
//
// Journal timestamps = historian:
//   * every entry carries a monotonic ts (injectable clock for determinism)
//   * replay_at(t) reconstructs the state "as of time t" (time-travel)
//   * timestamps do NOT affect replay: same ops -> same state, C3 intact

#include "test_runner.hpp"
#include "eafardb/table.hpp"

#include <cstring>

using namespace eafardb;

namespace {

// Deterministic clock: returns the value set by the test, then freezes.
// Simpler: a counter the test advances explicitly.
struct FakeClock {
    std::uint64_t t = 0;
    std::uint64_t operator()() { return t; }
};

} // namespace

TEST(journal_entries_are_timestamped) {
    Table t;
    t.add_column("v", ColumnType::Float64);
    t.insert(1);
    t.set_f(1, 0, 1.0);

    const auto& entries = t.journal().entries();
    CHECK(entries.size() >= 3u);
    // Default steady clock: ts strictly non-decreasing along the journal.
    for (std::size_t i = 1; i < entries.size(); ++i) {
        CHECK(entries[i].ts >= entries[i - 1].ts);
    }
    // All entries carry a nonzero timestamp (steady ms since epoch).
    for (const auto& e : entries) {
        CHECK(e.ts > 0);
    }
}

TEST(replay_at_reconstructs_state_as_of_time) {
    Table t;
    // Drive the timeline explicitly: each op gets ts = clock.t.
    FakeClock clock;
    t.set_journal_clock([&clock] { return clock.t; });
    t.add_column("v", ColumnType::Float64);

    clock.t = 1000;
    t.insert(1);
    clock.t = 2000;
    t.set_f(1, 0, 10.0);
    clock.t = 3000;
    t.insert(2);
    clock.t = 4000;
    t.set_f(2, 0, 20.0);
    clock.t = 5000;
    t.set_f(1, 0, 99.0); // key 1's final value

    // State at t=2500: only the first set_f happened -> v(1)=10, key2 absent.
    const Table at_2500 = Table::replay_at(t.journal(), 2500);
    CHECK(at_2500.contains(1));
    CHECK_EQ(at_2500.get_f(1, 0), 10.0);
    CHECK(!at_2500.contains(2));

    // State at t=4500: key2 present, key1 still 10 (last change is t=5000).
    const Table at_4500 = Table::replay_at(t.journal(), 4500);
    CHECK_EQ(at_4500.get_f(1, 0), 10.0);
    CHECK_EQ(at_4500.get_f(2, 0), 20.0);

    // State at t=9999: everything (equals full replay).
    const Table at_9999 = Table::replay_at(t.journal(), 9999);
    CHECK_EQ(at_9999.get_f(1, 0), 99.0);
    CHECK_EQ(at_9999.get_f(2, 0), 20.0);

    // Before any op: empty table.
    const Table at_0 = Table::replay_at(t.journal(), 0);
    CHECK_EQ(at_0.row_count(), 0u);
}

TEST(replay_at_works_inside_transactions) {
    Table t;
    FakeClock clock;
    t.set_journal_clock([&clock] { return clock.t; });
    t.add_column("v", ColumnType::Float64);

    clock.t = 100;
    t.begin_transaction();
    clock.t = 200;
    t.insert(1);
    clock.t = 300;
    t.set_f(1, 0, 5.0);
    clock.t = 400;
    t.commit();

    // Replay at 250: tx began, insert stamped at 200 happened, set_f at 300
    // has not. Time-travel is transactional by timestamp.
    const Table at_250 = Table::replay_at(t.journal(), 250);
    CHECK(at_250.contains(1));
    CHECK_EQ(at_250.get_f(1, 0), 0.0); // default, set_f not yet applied
    CHECK(at_250.journal().size() > 0u);
}

TEST(timestamps_do_not_affect_replay_determinism) {
    // Same ops with different clock speeds -> identical state (C3 intact).
    auto run = [](std::uint64_t step) {
        Table t;
        t.add_column("v", ColumnType::Float64);
        FakeClock clock;
        t.set_journal_clock([&clock] { return clock.t; });
        for (int64_t k = 0; k < 100; ++k) {
            clock.t += step;
            t.insert(k);
            clock.t += step;
            t.set_f(k, 0, static_cast<double>(k) * 0.5);
        }
        return t;
    };

    const Table fast = run(1);
    const Table slow = run(1000);
    // State identical; only timestamps differ.
    for (int64_t k = 0; k < 100; ++k) {
        CHECK_EQ(fast.get_f(k, 0), slow.get_f(k, 0));
    }
    CHECK(fast.journal().entries()[1].ts != slow.journal().entries()[1].ts);
}
