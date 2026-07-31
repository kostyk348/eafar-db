// eafardb/src/derived_view.cpp — S6 derived view implementation.

#include "eafardb/derived_view.hpp"

namespace eafardb {

DerivedView::DerivedView(std::string name, DependencyGraph& graph,
                         std::vector<std::pair<std::string, ValueSource*>> inputs,
                         CombineFn combine)
    : name_(std::move(name)), inputs_(), combine_(std::move(combine)) {
    graph.declare_node(name_);
    for (auto& [input_name, input] : inputs) {
        graph.add_edge(input_name, name_); // declared: throws on undeclared/cycle
        inputs_.push_back(input);
        last_versions_.push_back(input->version());
    }
}

double DerivedView::value() {
    bool changed = false;
    std::vector<double> vals;
    vals.reserve(inputs_.size());
    for (std::size_t i = 0; i < inputs_.size(); ++i) {
        ValueSource* src = inputs_[i];
        const double v = src->value(); // materializes upstream if needed
        vals.push_back(v);
        if (src->version() != last_versions_[i]) {
            last_versions_[i] = src->version();
            changed = true;
        }
    }
    if (changed) {
        cached_ = combine_(vals);
        ++recomputes_;
    }
    return cached_;
}

} // namespace eafardb
