// eafardb/tests/test_audit.cpp — S8 contract tests.
//
// Transactions in the journal:
//   * journal records tx boundaries (BeginTx/CommitTx markers)
//   * rollback truncates the uncommitted tail — including its markers
//   * replay reproduces exactly the post-commit state (no rollback leak)
//   * audit can enumerate every committed transaction's changes

#include "test_runner.hpp"
#include "eafardb/table.hpp"

#include <cstring>

using namespace eafardb;

namespace {

// Bit-exact state comparison via the journal contract (replay a copy).
void require_replay_equals(const Table& original) {
    const Table replayed = Table::replay(original.journal());
    CHECK_EQ(replayed.column_count(), original.column_count());
    for (std::uint32_t c = 0; c < original.column_count(); ++c) {
        if (original.column_type(c) == ColumnType::Int64) {
            original.scan_i(c, [&](int64_t k, int64_t v) {
                CHECK(replayed.contains(k));
                CHECK_EQ(replayed.get_i(k, c), v);
            });
        } else {
            original.scan_f(c, [&](int64_t k, double v) {
                CHECK(replayed.contains(k));
                const double rv = replayed.get_f(k, c);
                CHECK_EQ(std::memcmp(&v, &rv, sizeof(double)), 0);
            });
        }
    }
}

} // namespace

TEST(transaction_boundaries_recorded) {
    Table t;
    t.add_column("v", ColumnType::Float64);
    t.begin_transaction();
    t.insert(1);
    t.set_f(1, 0, 1.0);
    t.commit();

    const auto& entries = t.journal().entries();
    // AddColumn, BeginTx, Insert, SetF, CommitTx
    CHECK_EQ(entries.size(), 5u);
    CHECK(entries[1].op == JournalOp::BeginTx);
    CHECK(entries[4].op == JournalOp::CommitTx);
}

TEST(rollback_erases_uncommitted_tail_including_markers) {
    Table t;
    t.add_column("v", ColumnType::Float64);
    const std::size_t before = t.journal().size(); // just the AddColumn
    t.begin_transaction();
    t.insert(1);
    t.set_f(1, 0, 99.0);
    t.rollback();
    // The aborted transaction is not history: its entries AND markers
    // are gone.
    CHECK_EQ(t.journal().size(), before);
    CHECK_EQ(t.journal().transaction_ranges().size(), 0u);
    CHECK(!t.contains(1));
}

TEST(replay_across_transactions_no_rollback_leak) {
    Table t;
    t.add_column("v", ColumnType::Float64);
    t.insert(0);
    t.set_f(0, 0, 1.0);

    t.begin_transaction();
    t.insert(1);
    t.set_f(1, 0, 2.0);
    t.commit();

    t.begin_transaction();
    t.insert(2);
    t.set_f(2, 0, 3.0);
    t.rollback(); // must not leak into the journal

    t.begin_transaction();
    t.insert(3);
    t.set_f(3, 0, 4.0);
    t.commit();

    // Final committed state: keys 0, 1, 3.
    require_replay_equals(t);
    CHECK(t.contains(0));
    CHECK(t.contains(1));
    CHECK(!t.contains(2)); // rolled back
    CHECK(t.contains(3));

    // Audit: exactly two committed transactions.
    const auto ranges = t.journal().transaction_ranges();
    CHECK_EQ(ranges.size(), 2u);
}

TEST(audit_enumerates_per_tx_changes) {
    Table t;
    t.add_column("v", ColumnType::Float64);
    t.insert(0);

    t.begin_transaction();
    t.insert(1);
    t.set_f(1, 0, 10.0);
    t.commit();

    t.begin_transaction();
    t.insert(2);
    t.set_f(2, 0, 20.0);
    t.insert(3);
    t.set_f(3, 0, 30.0);
    t.commit();

    const auto ranges = t.journal().transaction_ranges();
    CHECK_EQ(ranges.size(), 2u);
    const auto& entries = t.journal().entries();

    // Tx1: BeginTx, Insert(1), SetF(1), CommitTx -> 4 entries.
    const auto& [b1, e1] = ranges[0];
    CHECK_EQ(e1 - b1, 4u);
    CHECK(entries[b1].op == JournalOp::BeginTx);
    CHECK(entries[e1 - 1].op == JournalOp::CommitTx);

    // Tx2: BeginTx, Insert(2), SetF(2), Insert(3), SetF(3), CommitTx -> 6.
    const auto& [b2, e2] = ranges[1];
    CHECK_EQ(e2 - b2, 6u);

    // The two tx ranges are adjacent and ordered (nothing between them).
    CHECK_EQ(e1, b2);
}

TEST(writes_outside_transactions_not_in_ranges) {
    Table t;
    t.add_column("v", ColumnType::Float64);
    t.insert(0); // outside any transaction

    t.begin_transaction();
    t.insert(1);
    t.commit();

    const auto ranges = t.journal().transaction_ranges();
    CHECK_EQ(ranges.size(), 1u);
    // The pre-tx writes are NOT part of the audited range: the range
    // starts at the BeginTx marker, after the pre-tx entries.
    const auto& [b, e] = ranges[0];
    CHECK(t.journal().entries()[b].op == JournalOp::BeginTx);
    CHECK(t.journal().entries()[e - 1].op == JournalOp::CommitTx);
    CHECK(b >= 1u); // at least the AddColumn is before the tx
    require_replay_equals(t);
}
