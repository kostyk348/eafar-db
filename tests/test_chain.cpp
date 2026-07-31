// eafardb/tests/test_chain.cpp — S6 contract tests.
//
// View dependency chain:
//   * views declare their inputs; the graph rejects undeclared nodes
//     and cycles at declaration time
//   * propagation is lazy and proportional: one write to the source
//     touches exactly the views on the path, nothing else
//   * a clean query recomputes nothing

#include "test_runner.hpp"
#include "eafardb/derived_view.hpp"
#include "eafardb/view.hpp"

using namespace eafardb;

namespace {

double scale10(const std::vector<double>& v) {
    return v[0] * 10.0;
}

} // namespace

TEST(chain_rejects_undeclared_edge) {
    DependencyGraph g;
    g.declare_node("src");
    g.declare_node("A");
    g.add_edge("src", "A");
    CHECK_THROWS(g.add_edge("ghost", "A"));
    CHECK_THROWS(g.add_edge("src", "ghost2"));
}

TEST(chain_rejects_cycle) {
    DependencyGraph g;
    g.declare_node("A");
    g.declare_node("B");
    g.add_edge("A", "B");
    // B already depends on A; making A depend on B closes a cycle.
    CHECK_THROWS(g.add_edge("B", "A"));
    // Self-edge is trivially a cycle.
    CHECK_THROWS(g.add_edge("A", "A"));
}

TEST(chain_propagates_lazily_and_selectively) {
    Table t(64);
    t.add_column("v", ColumnType::Float64);
    for (int64_t k = 0; k < 1000; ++k) {
        t.insert(k);
        t.set_f(k, 0, 1.0);
    }
    View A(t, 0);
    const std::uint64_t A_before = A.recomputed_pages();

    // B = A * 10 (declared edge A -> B). DerivedView registers itself.
    DependencyGraph g;
    g.declare_node("A");
    DerivedView B("B", g, {{"A", &A}}, scale10);

    // Materialize both: A full pass, B one recompute.
    CHECK_EQ(B.value(), 10000.0);
    const std::uint64_t A_materialized = A.recomputed_pages() - A_before;
    CHECK(A_materialized > 0);

    // One write to one page of the source.
    t.set_f(500, 0, 5.0);

    // Nothing recomputes until queried (lazy).
    const std::uint64_t A_after_write = A.recomputed_pages();
    CHECK_EQ(B.value(), 10040.0); // (999*1 + 5) * 10
    // Exactly one page recomputed in A, and B recomputed exactly once.
    CHECK_EQ(A.recomputed_pages(), A_after_write + 1);
    CHECK_EQ(B.recomputes(), 2u); // materialize + the propagation above

    // Clean query: nothing recomputes at all.
    const std::uint64_t A_clean = A.recomputed_pages();
    const std::uint64_t B_clean = B.recomputes();
    CHECK_EQ(B.value(), 10040.0);
    CHECK_EQ(A.recomputed_pages(), A_clean);
    CHECK_EQ(B.recomputes(), B_clean);
}

TEST(chain_three_views_touches_only_path) {
    Table t(64);
    t.add_column("v", ColumnType::Float64);
    for (int64_t k = 0; k < 1000; ++k) {
        t.insert(k);
        t.set_f(k, 0, 1.0);
    }
    View A(t, 0); // on the path
    View C(t, 0); // independent sibling, same source

    DependencyGraph g;
    g.declare_node("A");
    DerivedView B("B", g, {{"A", &A}}, scale10);

    CHECK_EQ(B.value(), 10000.0);
    const std::uint64_t C_after_materialize = C.recomputed_pages();

    t.set_f(700, 0, 7.0);
    CHECK_EQ(B.value(), 10060.0); // (999*1 + 7) * 10

    // B's chain recomputed its one dirty page; sibling C was NOT touched.
    CHECK(C.recomputed_pages() == C_after_materialize);
}

TEST(chain_double_hop) {
    Table t(64);
    t.add_column("v", ColumnType::Float64);
    for (int64_t k = 0; k < 1000; ++k) {
        t.insert(k);
        t.set_f(k, 0, 1.0);
    }
    View A(t, 0);
    DependencyGraph g;
    g.declare_node("A");
    DerivedView B("B", g, {{"A", &A}}, scale10);
    // C = B * 100. DerivedView registers C and declares B -> C itself.
    auto scale100 = [](const std::vector<double>& v) { return v[0] * 100.0; };
    DerivedView C("C", g, {{"B", &B}}, scale100);

    CHECK_EQ(C.value(), 1000000.0); // 1000 * 10 * 100

    t.set_f(100, 0, 2.0);
    CHECK_EQ(C.value(), 1001000.0); // (999*1 + 2) * 10 * 100
    // Each hop recomputed exactly once for this write.
    CHECK_EQ(B.recomputes(), 2u); // materialize + propagation
    CHECK_EQ(C.recomputes(), 2u);
}
