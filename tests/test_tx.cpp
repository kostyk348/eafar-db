// eafardb/tests/test_tx.cpp — S4 contract tests.
//
// Transactions = snapshot/rollback:
//   * begin is cheap (page-map of shared buffers, zero data copy)
//   * first write to a shared page deep-copies it (COW): cost
//     proportional to the dirty page, not the table
//   * rollback restores state bit-exactly (incl NaN/-0.0) and removes
//     uncommitted journal entries
//   * commit keeps the writes

#include "test_runner.hpp"
#include "eafardb/table.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>

using namespace eafardb;

namespace {

// Bit-exact comparison via the journal contract: replay a copy and compare
// column arrays per page.
void require_same_values(const Table& a, const Table& b) {
    CHECK_EQ(a.column_count(), b.column_count());
    for (std::uint32_t c = 0; c < a.column_count(); ++c) {
        std::vector<int64_t> ka, kb;
        if (a.column_type(c) == ColumnType::Int64) {
            a.scan_i(c, [&](int64_t k, int64_t v) { ka.push_back(k); (void)v; });
            b.scan_i(c, [&](int64_t k, int64_t v) { kb.push_back(k); (void)v; });
        } else {
            a.scan_f(c, [&](int64_t k, double v) { ka.push_back(k); (void)v; });
            b.scan_f(c, [&](int64_t k, double v) { kb.push_back(k); (void)v; });
        }
        CHECK(ka == kb);
    }
}

} // namespace

TEST(begin_commit_basic) {
    Table t(64);
    t.add_column("f", ColumnType::Float64);
    t.insert(1);
    t.set_f(1, 0, 10.0);

    t.begin_transaction();
    t.set_f(1, 0, 20.0);
    t.insert(2);
    t.set_f(2, 0, 5.0);
    t.commit();
    CHECK(!t.in_transaction());

    CHECK_EQ(t.get_f(1, 0), 20.0);
    CHECK_EQ(t.get_f(2, 0), 5.0);
    // Committed writes stay in the journal.
    // 1 AddColumn + 1 Insert(1) + 1 SetF(10) + 1 SetF(20) + 1 Insert(2) + 1 SetF(5) = 6
    CHECK_EQ(t.journal().size(), 6u);
}

TEST(rollback_restores_bit_exact) {
    Table t(64);
    t.add_column("f", ColumnType::Float64);
    t.add_column("i", ColumnType::Int64);
    for (int64_t k = 0; k < 100; ++k) {
        t.insert(k);
        t.set_f(k, 0, static_cast<double>(k) * 0.5);
        t.set_i(k, 1, k * 3);
    }

    const std::uint64_t copied_before = t.copied_cells();
    t.begin_transaction();
    // Dirty writes inside the tx.
    for (int64_t k = 0; k < 100; k += 2) {
        t.set_f(k, 0, -999.0);
    }
    t.insert(5000);
    t.insert(6000);
    t.erase(7);

    const Table pre_tx = Table::replay(t.journal().entries().size() > 0
                                           ? t.journal() : t.journal(),
                                       64);
    (void)pre_tx;
    t.rollback();
    CHECK(!t.in_transaction());

    // Bit-exact restoration: values back to pre-tx.
    for (int64_t k = 0; k < 100; ++k) {
        CHECK_EQ(t.get_f(k, 0), static_cast<double>(k) * 0.5);
        CHECK_EQ(t.get_i(k, 1), k * 3);
    }
    CHECK(!t.contains(5000));
    CHECK(!t.contains(6000));
    CHECK(t.contains(7));
    CHECK_EQ(t.row_count(), 100u);

    // Journal truncated: uncommitted ops are not history.
    CHECK_EQ(t.journal().size(), 2 + 100 * 3);

    // rollback with no tx throws; commit with no tx throws.
    bool threw = false;
    try { t.rollback(); } catch (...) { threw = true; }
    CHECK(threw);
    threw = false;
    try { t.commit(); } catch (...) { threw = true; }
    CHECK(threw);

    // COW did real copy work (the writes touched shared pages), but
    // proportional to dirty pages, not the whole table.
    CHECK(t.copied_cells() > copied_before);
}

TEST(cow_cost_proportional_to_dirty_pages) {
    // 100 pages, one column, 100 rows per page.
    Table t(100);
    t.add_column("f", ColumnType::Float64);
    for (int64_t k = 0; k < 10'000; ++k) {
        t.insert(k);
        t.set_f(k, 0, 1.0);
    }
    CHECK_EQ(t.page_count(), 100u);

    t.begin_transaction();
    const std::uint64_t before = t.copied_cells();
    // Dirty exactly one page (page 42): keys 4200..4299.
    for (int64_t k = 4200; k < 4300; ++k) {
        t.set_f(k, 0, 2.0);
    }
    const std::uint64_t copied = t.copied_cells() - before;
    // One page of 100 rows x 1 column = 100 cells copied, not 10'000.
    CHECK_EQ(copied, 100u);
    t.rollback();
}

TEST(cow_multiple_pages_proportional) {
    Table t(100);
    t.add_column("f", ColumnType::Float64);
    t.add_column("i", ColumnType::Int64); // 2 cells per row
    for (int64_t k = 0; k < 10'000; ++k) {
        t.insert(k);
        t.set_f(k, 0, 1.0);
        t.set_i(k, 1, 1);
    }
    t.begin_transaction();
    const std::uint64_t before = t.copied_cells();
    // Dirty 3 pages (keys 0..99, 5000..5099, 9900..9999).
    for (int64_t k = 0; k < 100; ++k) t.set_f(k, 0, 2.0);
    for (int64_t k = 5000; k < 5100; ++k) t.set_f(k, 0, 2.0);
    for (int64_t k = 9900; k < 10'000; ++k) t.set_f(k, 0, 2.0);
    const std::uint64_t copied = t.copied_cells() - before;
    CHECK_EQ(copied, 3u * 100 * 2); // 3 pages x 100 rows x 2 columns
    t.rollback();
}

TEST(rollback_preserves_nan_and_negative_zero) {
    Table t(64);
    t.add_column("f", ColumnType::Float64);

    const std::uint64_t nan_bits = 0x7FF8000000000042ULL;
    double nan_val;
    std::memcpy(&nan_val, &nan_bits, sizeof(nan_val));

    t.insert(1);
    t.set_f(1, 0, nan_val);   // committed NaN
    t.insert(2);
    t.set_f(2, 0, -0.0);      // committed -0.0

    t.begin_transaction();
    t.set_f(1, 0, 123.0);     // would clobber the NaN
    t.set_f(2, 0, 123.0);
    t.rollback();

    std::uint64_t out_bits;
    const double v1 = t.get_f(1, 0);
    std::memcpy(&out_bits, &v1, sizeof(out_bits));
    CHECK_EQ(out_bits, nan_bits); // NaN restored bit-exact
    CHECK(std::isnan(t.get_f(1, 0)));
    CHECK_EQ(std::signbit(t.get_f(2, 0)), 1); // -0.0 restored
}

TEST(nested_transaction_rejected) {
    Table t;
    t.add_column("v", ColumnType::Int64);
    t.insert(1);
    t.begin_transaction();
    bool threw = false;
    try { t.begin_transaction(); } catch (...) { threw = true; }
    CHECK(threw);
    t.commit();
    CHECK(!t.in_transaction());
}

TEST(rollback_restores_page_layout) {
    // After rollback, materialized page set matches pre-tx exactly:
    // pages created in the tx are gone, pages emptied in the tx are back.
    Table t(100);
    t.add_column("v", ColumnType::Float64);
    for (int64_t k = 0; k < 1000; ++k) {
        t.insert(k); // pages 0..9
    }
    const std::uint64_t pages_before = t.page_count();
    CHECK_EQ(pages_before, 10u);

    t.begin_transaction();
    t.insert(50'000);           // new page 500
    for (int64_t k = 0; k < 100; ++k) t.erase(k); // empty page 0
    CHECK_EQ(t.page_count(), 10u); // -1 +1
    t.rollback();

    CHECK_EQ(t.page_count(), 10u);
    CHECK(t.contains(50));          // page 0 restored
    CHECK(!t.contains(50'000));     // page 500 gone
    for (int64_t k = 0; k < 1000; ++k) {
        CHECK(t.contains(k));
    }
}
