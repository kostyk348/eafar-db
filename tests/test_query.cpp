// eafardb/tests/test_query.cpp — S7 contract tests.
//
// Fuzzy query:
//   * rows filtered by membership(field, concept) >= threshold
//   * threshold semantics exact at boundaries (ramp/triangle)
//   * membership swap changes the result set WITHOUT touching query code
//   * concept membership lives in the eafar core FuzzyRegistry (C2)

#include "test_runner.hpp"
#include "eafardb/query.hpp"

#include <algorithm>

using namespace eafardb;

namespace {

// Helper: table with column "v" = 0..99.
Table make_table() {
    Table t;
    t.add_column("v", ColumnType::Float64);
    for (int64_t k = 0; k < 100; ++k) {
        t.insert(k);
        t.set_f(k, 0, static_cast<double>(k));
    }
    return t;
}

} // namespace

TEST(fuzzy_ramp_boundary_semantics) {
    eafar::FuzzyRegistry reg;
    reg.define("warm", eafar::mf::ramp(20.0f, 60.0f));
    const Table t = make_table();
    FuzzyQuery q(reg);

    // Below lo -> 0, at/above hi -> 1, linear in between.
    // At boundary: v=20 -> 0.0, v=40 -> 0.5, v=60 -> 1.0.
    // membership >= 0.5  => v in [40, 99]  (v=40 gives exactly 0.5)
    // count = 99-40+1 = 60.
    CHECK_EQ(q.count(t, 0, "warm", 0.5f), 60u);
    // Strict: > 0.5 excludes v=40; [41,99] = 59.
    CHECK_EQ(q.count(t, 0, "warm", 0.51f), 59u);
    // Threshold 0 excludes nothing (every membership >= 0).
    CHECK_EQ(q.count(t, 0, "warm", 0.0f), 100u);
}

TEST(fuzzy_triangle_boundary) {
    eafar::FuzzyRegistry reg;
    // Center 50, width 25 -> support [25, 75], peak 1 at 50.
    reg.define("mid", eafar::mf::triangle(50.0f, 25.0f));
    const Table t = make_table();
    FuzzyQuery q(reg);

    // v=50 -> 1.0; v=25 and v=75 -> 0.0 (boundary, excluded at th>0).
    // membership >= 0.5 => |v-50| <= 12.5 => v in [37.5, 62.5] -> v in [38,62].
    const auto r = q.filter(t, 0, "mid", 0.5f);
    CHECK_EQ(r.size(), 25u); // 38..62 inclusive = 25
    CHECK_EQ(r.front(), 38);
    CHECK_EQ(r.back(), 62);
    // Threshold 1.0: only the peak (v=50).
    const auto peak = q.filter(t, 0, "mid", 1.0f);
    CHECK_EQ(peak.size(), 1u);
    CHECK_EQ(peak[0], 50);
}

TEST(fuzzy_membership_swap_changes_result_without_touching_query) {
    eafar::FuzzyRegistry reg;
    reg.define("hot", eafar::mf::step(80.0f));
    const Table t = make_table();
    FuzzyQuery q(reg);

    // step(80): membership 1 for v>=80 -> 20 keys.
    CHECK_EQ(q.count(t, 0, "hot", 0.5f), 20u);

    // Swap the function under the same name. The query code is untouched.
    reg.define("hot", eafar::mf::ramp(50.0f, 90.0f));
    // ramp(50,90) >= 0.5 => v >= 70 -> keys 70..99 = 30.
    CHECK_EQ(q.count(t, 0, "hot", 0.5f), 30u);

    // Swap again: strict step at 95.
    reg.define("hot", eafar::mf::step(95.0f));
    CHECK_EQ(q.count(t, 0, "hot", 0.5f), 5u); // 95..99
}

TEST(fuzzy_unknown_concept_throws) {
    eafar::FuzzyRegistry reg;
    const Table t = make_table();
    FuzzyQuery q(reg);
    CHECK_THROWS(q.count(t, 0, "not_defined", 0.5f));
}

TEST(fuzzy_threshold_0_returns_all_for_positive_domain) {
    eafar::FuzzyRegistry reg;
    reg.define("always", eafar::mf::step(0.0f));
    const Table t = make_table();
    FuzzyQuery q(reg);
    CHECK_EQ(q.count(t, 0, "always", 0.0f), 100u);
}
