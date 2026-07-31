// eafardb/src/view.cpp — S5 materialized view implementation.
//
// The automaton step: for each dirty page, re-read that page's column
// array (page-local scan only), update the page partial, and fold the
// delta into the cached global. A page that went back to sleep (erased
// empty) contributes 0 and is dropped from the partial map.

#include "eafardb/view.hpp"

namespace eafardb {

View::View(Table& source, std::uint32_t column)
    : source_(source), column_(column) {
    source_.attach(*this);
}

void View::on_page_written(std::uint64_t page_id) {
    dirty_.insert(page_id);
}

double View::value() {
    refresh();
    return cached_;
}

void View::refresh() {
    if (!initialized_) {
        // First query: materialize every currently materialized page.
        for (const std::uint64_t pid : source_.materialized_pages()) {
            dirty_.insert(pid);
        }
        initialized_ = true;
    }
    for (const std::uint64_t pid : dirty_) {
        double sum = 0.0;
        std::size_t rows = 0;
        if (source_.has_page(pid)) {
            const auto& col = source_.page_column_f(pid, column_);
            for (const double v : col) {
                sum += v;
            }
            rows = col.size();
        }
        ++recomputed_pages_;
        recomputed_rows_ += rows;
        const double old = partials_.contains(pid) ? partials_.at(pid) : 0.0;
        cached_ += sum - old;
        if (rows == 0) {
            partials_.erase(pid); // page asleep: no partial
        } else {
            partials_[pid] = sum;
        }
    }
    dirty_.clear();
}

} // namespace eafardb
