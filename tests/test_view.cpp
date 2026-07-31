// eafardb/tests/test_view.cpp — S5 contract tests.
//
// Materialized view as automaton:
//   * first query materializes all pages (full pass, counted)
//   * afterwards, work is proportional to dirty pages ONLY: one write
//     to one page => exactly one page recomputed
//   * result always equals a brute-force full scan (correctness anchor)

#include "test_runner.hpp"
#include "eafardb/view.hpp"

using namespace eafardb;

namespace {

double brute_force_sum(const Table& t, std::uint32_t col) {
    double sum = 0.0;
    t.scan_f(col, [&](int64_t, double v) { sum += v; });
    return sum;
}

} // namespace

TEST(sum_view_matches_brute_force) {
    Table t;
    t.add_column("v", ColumnType::Float64);
    for (int64_t k = 0; k < 10000; ++k) {
        t.insert(k);
        t.set_f(k, 0, static_cast<double>(k) * 0.25);
    }
    View v(t, 0);
    CHECK_EQ(v.value(), brute_force_sum(t, 0));
    // First query materialized everything, but the result is exact.
    CHECK(v.is_clean());
}

TEST(sum_view_materializes_all_pages_once) {
    Table t(64);
    t.add_column("v", ColumnType::Float64);
    for (int64_t k = 0; k < 100000; ++k) {
        t.insert(k);
        t.set_f(k, 0, 1.0);
    }
    View v(t, 0);
    const double first = v.value();
    CHECK_EQ(first, 100000.0);
    const std::uint64_t materialized_pages = 100000u / 64u + 1u; // 1562 full + 1 partial
    CHECK_EQ(v.recomputed_pages(), materialized_pages);
    // Clean query: no recompute at all.
    const std::uint64_t before = v.recomputed_pages();
    CHECK_EQ(v.value(), 100000.0);
    CHECK_EQ(v.recomputed_pages(), before);
}

TEST(sum_view_single_write_recomputes_single_page) {
    Table t(64);
    t.add_column("v", ColumnType::Float64);
    for (int64_t k = 0; k < 100000; ++k) {
        t.insert(k);
        t.set_f(k, 0, 1.0);
    }
    View v(t, 0);
    CHECK_EQ(v.value(), 100000.0);
    CHECK(v.is_clean());

    // One write to one page. Exactly that page must be recomputed.
    const std::uint64_t pages_before = v.recomputed_pages();
    const std::uint64_t rows_before = v.recomputed_rows();
    t.set_f(12345, 0, 999.0);
    CHECK(!v.is_clean()); // event arrived, not yet recomputed
    CHECK_EQ(v.value(), 100000.0 - 1.0 + 999.0);
    CHECK_EQ(v.recomputed_pages(), pages_before + 1);
    // The recomputed page holds 64 rows (page_size 64).
    CHECK_EQ(v.recomputed_rows(), rows_before + 64);
    CHECK(v.is_clean());
    // And the incremental result equals a full brute-force scan.
    CHECK_EQ(v.value(), brute_force_sum(t, 0));
}

TEST(sum_view_multiple_writes_same_page_one_recompute) {
    Table t(64);
    t.add_column("v", ColumnType::Float64);
    for (int64_t k = 0; k < 10000; ++k) {
        t.insert(k);
        t.set_f(k, 0, 0.0);
    }
    View v(t, 0);
    CHECK_EQ(v.value(), 0.0);

    // 10 writes, all to the same page -> 1 recompute of that page.
    const std::uint64_t before = v.recomputed_pages();
    for (int i = 0; i < 10; ++i) {
        t.set_f(100 + i, 0, 5.0);
    }
    CHECK_EQ(v.value(), 50.0);
    CHECK_EQ(v.recomputed_pages(), before + 1);
    CHECK_EQ(v.value(), brute_force_sum(t, 0));
}

TEST(sum_view_tracks_insert_and_erase) {
    Table t;
    t.add_column("v", ColumnType::Float64);
    for (int64_t k = 0; k < 1000; ++k) {
        t.insert(k);
        t.set_f(k, 0, 2.0);
    }
    View v(t, 0);
    CHECK_EQ(v.value(), 2000.0);

    t.insert(5000); // new page
    t.set_f(5000, 0, 7.0);
    CHECK_EQ(v.value(), 2007.0);

    t.erase(5000); // page goes back to sleep
    CHECK_EQ(v.value(), 2000.0);
    CHECK_EQ(v.value(), brute_force_sum(t, 0));
    // 4 pages (1000 keys / 256) materialized + insert page 19 + erase page 19
    CHECK_EQ(v.recomputed_pages(), 4u + 1 + 1);
}

TEST(sum_view_works_after_rollback) {
    Table t(64);
    t.add_column("v", ColumnType::Float64);
    for (int64_t k = 0; k < 1000; ++k) {
        t.insert(k);
        t.set_f(k, 0, 1.0);
    }
    View v(t, 0);
    CHECK_EQ(v.value(), 1000.0);

    t.begin_transaction();
    t.set_f(500, 0, 500.0);
    t.rollback();
    // Rollback restores the table; the view must see the restored state.
    CHECK_EQ(v.value(), 1000.0);
    CHECK_EQ(v.value(), brute_force_sum(t, 0));
}
