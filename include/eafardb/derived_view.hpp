#pragma once
// eafardb/derived_view.hpp — S6: views on views (dependency chain).
//
// A DerivedView declares its inputs via a DependencyGraph and combines
// them lazily: it recomputes only when an input's version actually grew
// (i.e. the input did real work). This gives:
//   * declared edges only — the graph rejects undeclared nodes
//   * cycle rejection — a view cannot depend on itself transitively
//   * lazy propagation — a downstream view recomputes only when its
//     upstream source really changed

#include "eafardb/dependency_graph.hpp"
#include "eafardb/view.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace eafardb {

// Combines materialized input values into this view's value.
using CombineFn = std::function<double(const std::vector<double>&)>;

class DerivedView : public ValueSource {
public:
    // Registers `name` in `graph` with edges from each declared input.
    // Throws on duplicate name, undeclared input, or a cycle.
    DerivedView(std::string name, DependencyGraph& graph,
                std::vector<std::pair<std::string, ValueSource*>> inputs,
                CombineFn combine);

    // Lazy: queries each input (which materializes it if needed) and
    // recomputes only if some input's version grew since last query.
    double value() override;

    // Monotonic: grows on every actual recompute.
    std::uint64_t version() const override { return recomputes_; }

    std::uint64_t recomputes() const { return recomputes_; }
    const std::string& name() const { return name_; }

private:
    std::string name_;
    std::vector<ValueSource*> inputs_;
    std::vector<std::uint64_t> last_versions_;
    CombineFn combine_;
    double cached_ = 0.0;
    std::uint64_t recomputes_ = 0;
};

} // namespace eafardb
