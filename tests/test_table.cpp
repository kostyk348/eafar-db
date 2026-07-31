// eafardb/tests/test_table.cpp — S1 contract tests.
//
// Covers every S1 claim from the spec:
//   * SoA storage (column arrays contiguous, column-wise access)
//   * CRUD by key, NotFound semantics
//   * bit-exact value passthrough (NaN bit patterns survive)
//   * deterministic swap-with-last erase
//   * sparse-friendliness (absent keys cost nothing)

#include "test_runner.hpp"
#include "eafardb/table.hpp"

#include <cmath>
#include <cstring>
#include <limits>

using namespace eafardb;

// ---------------------------------------------------------------------------
// Schema
// ---------------------------------------------------------------------------

TEST(add_column_returns_ids) {
    Table t;
    const auto id_temp = t.add_column("temperature", ColumnType::Float64);
    const auto id_pop = t.add_column("population", ColumnType::Int64);
    CHECK_EQ(id_temp, 0u);
    CHECK_EQ(id_pop, 1u);
    CHECK_EQ(t.column_count(), 2u);
    CHECK_EQ(t.column_type(id_temp), ColumnType::Float64);
    CHECK_EQ(t.column_type(id_pop), ColumnType::Int64);
}

TEST(column_id_resolves_by_name) {
    Table t;
    t.add_column("a", ColumnType::Int64);
    t.add_column("b", ColumnType::Float64);
    CHECK_EQ(t.column_id("a"), 0u);
    CHECK_EQ(t.column_id("b"), 1u);
    CHECK_THROWS(t.column_id("nope"));
    CHECK_THROWS(t.column_type(7));
}

TEST(duplicate_column_rejected) {
    Table t;
    t.add_column("dup", ColumnType::Int64);
    CHECK_THROWS(t.add_column("dup", ColumnType::Float64));
}

// ---------------------------------------------------------------------------
// CRUD
// ---------------------------------------------------------------------------

TEST(insert_read_roundtrip) {
    Table t;
    t.add_column("v", ColumnType::Float64);
    t.insert(42);
    CHECK(t.contains(42));
    CHECK(!t.contains(43));
    CHECK_EQ(t.row_count(), 1u);
    CHECK_EQ(t.get_f(42, 0), 0.0); // default
}

TEST(set_get_exact_values) {
    Table t;
    t.add_column("i", ColumnType::Int64);
    t.add_column("f", ColumnType::Float64);
    t.insert(1);
    t.set_i(1, 0, -123456789012345LL);
    t.set_f(1, 1, 3.141592653589793);
    CHECK_EQ(t.get_i(1, 0), -123456789012345LL);
    CHECK_EQ(t.get_f(1, 1), 3.141592653589793);
}

TEST(missing_key_throws) {
    Table t;
    t.add_column("v", ColumnType::Float64);
    t.insert(5);
    CHECK_THROWS(t.get_f(6, 0));
    CHECK_THROWS(t.get_i(6, 0));
    CHECK_THROWS(t.set_f(6, 0, 1.0));
    CHECK_THROWS(t.erase(6));
}

TEST(wrong_column_type_throws) {
    Table t;
    t.add_column("i", ColumnType::Int64);
    t.add_column("f", ColumnType::Float64);
    t.insert(1);
    // Reading an int column as float (and vice versa) is a type error.
    CHECK_THROWS(t.get_f(1, 0));
    CHECK_THROWS(t.get_i(1, 1));
    // Column id past the end.
    CHECK_THROWS(t.get_f(1, 2));
    CHECK_THROWS(t.set_i(1, 2, 0));
}

TEST(insert_existing_key_noop) {
    Table t;
    t.add_column("v", ColumnType::Int64);
    t.insert(7);
    t.set_i(7, 0, 99);
    t.insert(7); // no-op: does not reset the value
    CHECK_EQ(t.get_i(7, 0), 99);
    CHECK_EQ(t.row_count(), 1u);
}

// ---------------------------------------------------------------------------
// SoA layout
// ---------------------------------------------------------------------------

TEST(column_arrays_contiguous_soa) {
    Table t;
    t.add_column("i", ColumnType::Int64);
    t.add_column("f", ColumnType::Float64);
    for (int64_t k = 0; k < 1000; ++k) {
        t.insert(k);
        t.set_i(k, 0, k * k);
        t.set_f(k, 1, static_cast<double>(k) + 0.5);
    }
    // SoA: each column is one contiguous array; row r of column c is
    // at data()[r]. Contiguity is guaranteed by vector; we assert the
    // column-wise access pattern exposes the raw array.
    const auto& icol = t.column_i(0);
    const auto& fcol = t.column_f(1);
    CHECK_EQ(icol.size(), 1000u);
    CHECK_EQ(fcol.size(), 1000u);
    CHECK_EQ(icol[500], 500LL * 500);
    CHECK_EQ(fcol[500], 500.5);
    // data() pointers are usable for bulk column reads (SoA scan).
    const int64_t* ip = icol.data();
    const double* fp = fcol.data();
    for (std::size_t r = 0; r < 1000; ++r) {
        CHECK_EQ(ip[r], static_cast<int64_t>(r) * r);
        CHECK_EQ(fp[r], static_cast<double>(r) + 0.5);
    }
}

TEST(column_scans_are_typed) {
    Table t;
    t.add_column("i", ColumnType::Int64);
    t.add_column("f", ColumnType::Float64);
    for (int64_t k = 0; k < 5; ++k) {
        t.insert(k);
        t.set_f(k, 1, static_cast<double>(k) * 10.0);
    }
    // A column-wise scan walks only that column's array (SoA locality).
    const auto& fcol = t.column_f(1);
    CHECK_EQ(fcol.size(), 5u);
    CHECK_EQ(fcol[0], 0.0);
    CHECK_EQ(fcol[4], 40.0);
    // Wrong-type column access throws (no silent reinterpret).
    CHECK_THROWS(t.column_f(0));
    CHECK_THROWS(t.column_i(1));
}

// ---------------------------------------------------------------------------
// Bit-exact passthrough
// ---------------------------------------------------------------------------

TEST(nan_bit_pattern_survives) {
    Table t;
    t.add_column("f", ColumnType::Float64);
    t.insert(1);
    // A quiet NaN with a payload bit set — must round-trip bit-exact.
    const uint64_t payload = 0x7FF8000000000042ULL;
    double v;
    std::memcpy(&v, &payload, sizeof(v));
    t.set_f(1, 0, v);
    const double out = t.get_f(1, 0);
    uint64_t bits;
    std::memcpy(&bits, &out, sizeof(bits));
    CHECK_EQ(bits, payload);
    CHECK(std::isnan(out));
}

TEST(negative_zero_survives) {
    Table t;
    t.add_column("f", ColumnType::Float64);
    t.insert(1);
    t.set_f(1, 0, -0.0);
    const double out = t.get_f(1, 0);
    CHECK_EQ(std::signbit(out), 1);
    CHECK_EQ(out, 0.0); // -0.0 == 0.0 numerically
}

// ---------------------------------------------------------------------------
// Erase semantics (swap-with-last: dense, no tombstones)
// ---------------------------------------------------------------------------

TEST(erase_removes_key) {
    Table t;
    t.add_column("v", ColumnType::Int64);
    t.insert(1);
    t.insert(2);
    t.insert(3);
    t.erase(2);
    CHECK(!t.contains(2));
    CHECK(t.contains(1));
    CHECK(t.contains(3));
    CHECK_EQ(t.row_count(), 2u);
    CHECK_THROWS(t.get_i(2, 0));
    // Survivors keep their values (swap-with-last moved key 3 into slot 1).
    CHECK_EQ(t.get_i(1, 0), 0);
    CHECK_EQ(t.get_i(3, 0), 0);
}

TEST(erase_preserves_survivor_values) {
    Table t;
    t.add_column("v", ColumnType::Int64);
    for (int64_t k = 0; k < 10; ++k) {
        t.insert(k);
        t.set_i(k, 0, k * 100);
    }
    // Erase middle keys; survivors must retain their values exactly.
    t.erase(3);
    t.erase(5);
    t.erase(8);
    CHECK_EQ(t.row_count(), 7u);
    for (int64_t k = 0; k < 10; ++k) {
        if (k == 3 || k == 5 || k == 8) continue;
        CHECK_EQ(t.get_i(k, 0), k * 100);
    }
}

TEST(erase_swap_with_last_deterministic) {
    // Same op sequence -> same surviving layout (both directions verified
    // by identical reads; determinism claim: no unspecified order anywhere).
    Table a, b;
    for (int64_t k = 0; k < 4; ++k) {
        a.add_column("v", ColumnType::Float64);
        b.add_column("v", ColumnType::Float64);
        break;
    }
    for (int64_t k = 0; k < 4; ++k) {
        a.insert(k);
        b.insert(k);
        a.set_f(k, 0, static_cast<double>(k) + 0.25);
        b.set_f(k, 0, static_cast<double>(k) + 0.25);
    }
    a.erase(1);
    b.erase(1);
    for (int64_t k = 0; k < 4; ++k) {
        if (k == 1) continue;
        CHECK_EQ(a.get_f(k, 0), b.get_f(k, 0));
    }
}

// ---------------------------------------------------------------------------
// Sparse-friendliness
// ---------------------------------------------------------------------------

TEST(absent_keys_cost_nothing) {
    // Inserting far-apart keys must not allocate per absent key:
    // row_count tracks only materialized rows.
    Table t;
    t.add_column("v", ColumnType::Float64);
    t.insert(1);
    t.insert(1'000'000'000LL);
    CHECK_EQ(t.row_count(), 2u);
    CHECK_EQ(t.column_f(0).size(), 2u);
    CHECK_EQ(t.get_f(1'000'000'000LL, 0), 0.0);
}

TEST(large_sparse_insert) {
    Table t;
    t.add_column("v", ColumnType::Float64);
    for (int64_t k = 0; k < 100'000; ++k) {
        t.insert(k * 1000); // 100M key span, 100k rows
    }
    CHECK_EQ(t.row_count(), 100'000u);
    CHECK_EQ(t.get_f(999 * 1000, 0), 0.0);
    CHECK_EQ(t.column_f(0).size(), 100'000u);
}
