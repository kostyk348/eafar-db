// eafardb/tests/test_pages.cpp — S2 contract tests.
//
// The S2 thesis (core of the paradigm in DB form):
//   scan cost is proportional to materialized pages, never to the key
//   range or the number of rows. A table over a huge key space with
//   writes in a single page scans in O(page), not O(range).

#include "test_runner.hpp"
#include "eafardb/table.hpp"

#include <cstdint>
#include <vector>

using namespace eafardb;

TEST(page_of_key_is_deterministic) {
    Table t(1024);
    CHECK_EQ(t.page_id(0), 0u);
    CHECK_EQ(t.page_id(1023), 0u);
    CHECK_EQ(t.page_id(1024), 1u);
    CHECK_EQ(t.page_id(2'048'000), 2000u);
}

TEST(page_count_tracks_materialized_pages) {
    Table t(1024);
    t.add_column("v", ColumnType::Float64);
    CHECK_EQ(t.page_count(), 0u); // empty table: no pages exist

    t.insert(0);
    CHECK_EQ(t.page_count(), 1u);
    t.insert(1023); // same page (0)
    CHECK_EQ(t.page_count(), 1u);
    t.insert(1024); // new page (1)
    CHECK_EQ(t.page_count(), 2u);
    t.insert(10'000); // page 9 (10'000/1024 = 9.76 -> 9)
    CHECK_EQ(t.page_count(), 3u);
}

TEST(page_goes_back_to_sleep_when_emptied) {
    Table t(1024);
    t.add_column("v", ColumnType::Float64);
    t.insert(5);
    t.insert(2000);
    CHECK_EQ(t.page_count(), 2u);
    t.erase(5);
    CHECK_EQ(t.page_count(), 1u); // page 0 is empty -> gone (sleeping)
    t.erase(2000);
    CHECK_EQ(t.page_count(), 0u); // all pages asleep
}

// --- The thesis: 1M key space, 1 written page, scan touches 1 page ---

TEST(scan_touches_only_materialized_pages) {
    Table t(1024); // 1024 keys per page
    t.add_column("v", ColumnType::Float64);

    // Write rows in a single page (page 500: keys 512'000..512'999).
    const int64_t page_start = 500 * 1024;
    for (int64_t i = 0; i < 1000; ++i) {
        t.insert(page_start + i);
        t.set_f(page_start + i, 0, static_cast<double>(i));
    }
    CHECK_EQ(t.page_count(), 1u); // exactly one materialized page

    // Full scan over the whole 1M+ key space: must visit exactly 1 page.
    const std::uint64_t before = t.touched_pages();
    std::uint64_t rows = 0;
    t.scan_f(0, [&](int64_t, double) { ++rows; });
    CHECK_EQ(rows, 1000u);
    CHECK_EQ(t.touched_pages() - before, 1u); // ONE page touched, not 1000
}

TEST(ranged_scan_touches_only_pages_in_range) {
    Table t(1024);
    t.add_column("v", ColumnType::Float64);

    // Materialize three pages: 0, 500, 900.
    t.insert(10);
    t.set_f(10, 0, 1.0);
    const int64_t p500 = 500 * 1024;
    t.insert(p500);
    t.set_f(p500, 0, 2.0);
    const int64_t p900 = 900 * 1024;
    t.insert(p900);
    t.set_f(p900, 0, 3.0);
    CHECK_EQ(t.page_count(), 3u);

    // Scan a range covering only page 500: one page touched.
    const std::uint64_t before = t.touched_pages();
    std::vector<double> seen;
    t.scan_range_f(0, p500, p500 + 1023, [&](int64_t k, double v) {
        seen.push_back(v);
    });
    CHECK_EQ(seen.size(), 1u);
    CHECK_EQ(seen[0], 2.0);
    CHECK_EQ(t.touched_pages() - before, 1u);

    // Range spanning pages 500..900: exactly two pages touched.
    const std::uint64_t before2 = t.touched_pages();
    std::vector<int64_t> keys;
    t.scan_range_f(0, p500, p900, [&](int64_t k, double) { keys.push_back(k); });
    CHECK_EQ(keys.size(), 2u);
    CHECK_EQ(keys[0], p500);
    CHECK_EQ(keys[1], p900);
    CHECK_EQ(t.touched_pages() - before2, 2u);
}

TEST(scan_ascending_key_order) {
    Table t(64);
    t.add_column("v", ColumnType::Int64);
    // Insert out of order; scan must yield ascending keys.
    t.insert(300);
    t.insert(10);
    t.insert(250);
    t.insert(5);
    t.insert(1000);
    std::vector<int64_t> keys;
    t.scan_i(0, [&](int64_t k, int64_t) { keys.push_back(k); });
    const std::vector<int64_t> expected = {5, 10, 250, 300, 1000};
    CHECK(keys == expected);
}

TEST(scan_soa_column_locality) {
    // Scan reads the column array directly (SoA): for 100k rows across
    // 100 pages, exactly 100 pages are touched — one per materialized
    // page, regardless of row count.
    Table t(1000);
    t.add_column("v", ColumnType::Float64);
    for (int64_t i = 0; i < 100'000; ++i) {
        t.insert(i); // contiguous keys 0..99'999 -> pages 0..99
    }
    CHECK_EQ(t.page_count(), 100u);

    const std::uint64_t before = t.touched_pages();
    double sum = 0.0;
    t.scan_f(0, [&](int64_t, double v) { sum += v; });
    CHECK_EQ(t.touched_pages() - before, 100u); // pages, not rows
    CHECK_EQ(sum, 0.0);
}

TEST(scan_does_not_allocate_for_empty_ranges) {
    Table t(1024);
    t.add_column("v", ColumnType::Float64);
    t.insert(5);
    t.set_f(5, 0, 1.0);

    // Range with no materialized rows: zero callbacks, zero pages.
    const std::uint64_t before = t.touched_pages();
    std::size_t calls = 0;
    t.scan_range_f(0, 1'000'000, 2'000'000, [&](int64_t, double) { ++calls; });
    CHECK_EQ(calls, 0u);
    CHECK_EQ(t.touched_pages() - before, 0u);
}
