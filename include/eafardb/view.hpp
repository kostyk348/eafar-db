#pragma once
// eafardb/view.hpp — S5: Materialized view as automaton.
//
// Paradigm: a view is an automaton.
//   events   = page-touching writes on the source table (notify_views)
//   state    = per-page partials + cached global value
//   step     = recompute ONLY the pages that received events (lazy, on query)
//
// Work is proportional to dirty pages, never to the table: after full
// materialization, one write to one page recomputes one page partial.

#include "eafardb/table.hpp"

#include <cstdint>
#include <map>
#include <set>
#include <vector>

namespace eafardb {

// A node in a view chain: something that can be materialized (lazily) and
// reports a monotonic version that grows on every actual recompute.
// Consumers compare versions to decide whether to recompute themselves
// (S6 lazy propagation).
class ValueSource {
public:
    virtual ~ValueSource() = default;
    // Materialize if dirty and return the current value.
    virtual double value() = 0;
    // Grows monotonically every time value() actually recomputes.
    virtual std::uint64_t version() const = 0;
};

// SUM view over one float column of a table. Query returns the cached
// value; on query, dirty pages (evented since last query) are recomputed
// from the page's column array only.
class View : public ValueSource {
public:
    // Subscribes to `source`: from now on every page-touching mutation
    // of the source marks the page dirty here.
    View(Table& source, std::uint32_t column);

    // Returns the materialized value, recomputing dirty pages first
    // (lazy: nothing is recomputed until queried).
    double value() override;

    // Events from the source table (called by Table::notify_views).
    void on_page_written(std::uint64_t page_id);

    // ValueSource: version = number of pages recomputed so far.
    std::uint64_t version() const override { return recomputed_pages_; }

    // Counters for the S5 proof: pages/rows recomputed since construction.
    std::uint64_t recomputed_pages() const { return recomputed_pages_; }
    std::uint64_t recomputed_rows() const { return recomputed_rows_; }
    // True if no event has arrived since the last refresh (fully cached).
    bool is_clean() const { return dirty_.empty() && initialized_; }

private:
    void refresh(); // recompute every dirty page; recompute global from partials

    Table& source_;
    std::uint32_t column_;
    std::map<std::uint64_t, double> partials_; // page_id -> page sum
    std::set<std::uint64_t> dirty_;            // pages with pending events
    bool initialized_ = false;                 // first query materializes all
    double cached_ = 0.0;                      // global SUM
    std::uint64_t recomputed_pages_ = 0;
    std::uint64_t recomputed_rows_ = 0;
};

} // namespace eafardb
