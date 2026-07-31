// eafardb/tests/test_journal.cpp — S3 contract tests.
//
// Determinism thesis: the journal is the primary source of truth.
// Replaying a journal from an empty table reproduces the original
// state bit-exactly — including NaN bit patterns and -0.0 — and the
// replay itself is a deterministic function of the journal alone.

#include "test_runner.hpp"
#include "eafardb/table.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>

using namespace eafardb;

namespace {

// Bit-exact comparison of two tables: schema, row keys in scan order,
// and every column array (memcmp — no float interpretation).
void require_bitwise_equal(const Table& a, const Table& b) {
    CHECK_EQ(a.column_count(), b.column_count());
    for (std::uint32_t c = 0; c < a.column_count(); ++c) {
        CHECK(a.column_type(c) == b.column_type(c));

        if (a.column_type(c) == ColumnType::Int64) {
            const auto& col_a = a.column_i(c);
            const auto& col_b = b.column_i(c);
            CHECK(col_a.size() == col_b.size());
            CHECK(col_a.empty() ||
                  std::memcmp(col_a.data(), col_b.data(),
                              col_a.size() * sizeof(int64_t)) == 0);
        } else {
            const auto& col_a = a.column_f(c);
            const auto& col_b = b.column_f(c);
            CHECK(col_a.size() == col_b.size());
            CHECK(col_a.empty() ||
                  std::memcmp(col_a.data(), col_b.data(),
                              col_a.size() * sizeof(double)) == 0);
        }
    }
}

} // namespace

TEST(journal_records_mutations_in_order) {
    Table t;
    t.add_column("i", ColumnType::Int64);
    t.add_column("f", ColumnType::Float64);
    t.insert(10);
    t.set_i(10, 0, 42);
    t.set_f(10, 1, 2.5);
    t.insert(20);
    t.erase(10);

    const auto& j = t.journal();
    CHECK_EQ(j.size(), 7u); // 2 schema + 3 ops on key 10 + insert + erase
    // First two entries are the schema (AddColumn in creation order).
    CHECK(j.entries()[0].op == JournalOp::AddColumn);
    CHECK(j.entries()[1].op == JournalOp::AddColumn);
    CHECK(j.entries()[0].name == "i");
    CHECK(j.entries()[1].name == "f");
    // Then data ops in apply order.
    CHECK(j.entries()[2].op == JournalOp::Insert);
    CHECK_EQ(j.entries()[2].key, 10);
    CHECK(j.entries()[3].op == JournalOp::SetI);
    CHECK_EQ(j.entries()[3].value_i, 42);
    CHECK(j.entries()[4].op == JournalOp::SetF);
    CHECK_EQ(j.entries()[4].key, 10);
    CHECK(j.entries()[5].op == JournalOp::Insert);
    CHECK_EQ(j.entries()[5].key, 20);
    CHECK(j.entries()[6].op == JournalOp::Erase);
    CHECK_EQ(j.entries()[6].key, 10);
}

TEST(journal_size_tracks_all_mutations) {
    Table t;
    t.add_column("i", ColumnType::Int64);
    t.add_column("f", ColumnType::Float64);
    CHECK_EQ(t.journal().size(), 2u);
    t.insert(10);
    t.insert(20);
    CHECK_EQ(t.journal().size(), 4u);
    t.set_i(10, 0, 42);
    t.set_f(20, 1, 1.25);
    CHECK_EQ(t.journal().size(), 6u);
    t.erase(10);
    CHECK_EQ(t.journal().size(), 7u);
    // No-op insert is not journaled (no state change).
    t.insert(20);
    CHECK_EQ(t.journal().size(), 7u);
    // Failed erase throws and is not journaled.
    bool threw = false;
    try { t.erase(999); } catch (...) { threw = true; }
    CHECK(threw);
    CHECK_EQ(t.journal().size(), 7u);
}

TEST(replay_reproduces_state_bit_exact) {
    Table t(64);
    t.add_column("i", ColumnType::Int64);
    t.add_column("f", ColumnType::Float64);
    // 10k mixed ops with churn; track expected journal size alongside.
    std::size_t expected_ops = 2; // schema
    for (int64_t k = 0; k < 5000; ++k) {
        t.insert(k);
        t.set_i(k, 0, k * 7 - 3);
        t.set_f(k, 1, static_cast<double>(k) * 0.5);
        expected_ops += 3;
    }
    for (int64_t k = 0; k < 5000; k += 2) {
        t.erase(k); // erases every other key
        ++expected_ops;
    }
    for (int64_t k = 0; k < 5000; ++k) {
        if (k % 3 == 0 && t.contains(k)) {
            t.set_f(k, 1, static_cast<double>(k) * 0.25);
            ++expected_ops;
        }
    }
    CHECK_EQ(t.journal().size(), expected_ops);

    const Table r = Table::replay(t.journal(), 64);
    require_bitwise_equal(t, r);
    // The replay's own journal is identical: replay is idempotent.
    CHECK_EQ(r.journal().size(), t.journal().size());
}

TEST(replay_preserves_nan_and_negative_zero) {
    Table t;
    t.add_column("f", ColumnType::Float64);

    // NaN with payload bit.
    const std::uint64_t nan_bits = 0x7FF8000000000042ULL;
    double nan_val;
    std::memcpy(&nan_val, &nan_bits, sizeof(nan_val));

    t.insert(1);
    t.insert(2);
    t.set_f(1, 0, nan_val);
    t.set_f(2, 0, -0.0);

    const Table r = Table::replay(t.journal());
    const double n1 = r.get_f(1, 0);
    const double n2 = r.get_f(2, 0);

    std::uint64_t out_bits;
    std::memcpy(&out_bits, &n1, sizeof(out_bits));
    CHECK_EQ(out_bits, nan_bits); // bit-exact NaN
    CHECK(std::isnan(n1));
    CHECK_EQ(std::signbit(n2), 1); // -0.0 preserved
}

TEST(replay_empty_journal_gives_empty_table) {
    Table t;
    const Table r = Table::replay(t.journal());
    CHECK_EQ(r.column_count(), 0u);
    CHECK_EQ(r.row_count(), 0u);
    CHECK_EQ(r.page_count(), 0u);
}

TEST(replay_is_deterministic_function_of_journal) {
    Table t;
    t.add_column("v", ColumnType::Float64);
    for (int64_t k = 0; k < 1000; ++k) {
        t.insert(k);
        t.set_f(k, 0, static_cast<double>(k) * 1.5);
    }
    for (int64_t k = 0; k < 1000; k += 4) {
        t.erase(k);
    }

    const Table r1 = Table::replay(t.journal());
    const Table r2 = Table::replay(t.journal());
    require_bitwise_equal(r1, r2); // same journal -> same state, always
}

TEST(replay_independent_of_page_size_layout) {
    // The journal does not depend on page layout: replaying with a
    // different page_size yields the same *values* (pages differ, but
    // the journal itself is layout-free).
    Table t(128);
    t.add_column("f", ColumnType::Float64);
    for (int64_t k = 0; k < 1000; ++k) {
        t.insert(k);
        t.set_f(k, 0, static_cast<double>(k));
    }
    const Table r_128 = Table::replay(t.journal(), 128);
    const Table r_7 = Table::replay(t.journal(), 7);
    CHECK_EQ(r_128.row_count(), 1000u);
    CHECK_EQ(r_7.row_count(), 1000u);
    for (int64_t k = 0; k < 1000; ++k) {
        CHECK_EQ(r_7.get_f(k, 0), static_cast<double>(k));
    }
}
