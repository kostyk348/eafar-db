#pragma once
// eafardb/table.hpp — S1-S4: Columnar table storage over physical sparse
// pages, write journal, transactions = snapshot/rollback (COW).
//
// The paradigm's "everything is a field" applied to a database:
// a table is a set of named fields (columns), stored SoA — one contiguous
// array per column *within each page* (block-partitioned columnar, like
// Parquet/ClickHouse row groups). A "row" is an index across the page's
// arrays; there is no per-row object.
//
// Keys are int64. Storage is physically sparse: a page exists only if it
// holds at least one row — a sleeping page is zero memory. Waking happens
// on first write. Scans iterate pages (ascending) and rows within pages
// (ascending key): scan cost is proportional to materialized pages, never
// to the key range (S2 thesis).
//
// Every mutation is recorded in a write journal (S3). The journal is the
// primary source of truth: Table::replay() reconstructs bit-identical
// state (including NaN bit patterns and -0.0) from an empty table.
//
// Transactions (S4): begin_transaction() takes a cheap snapshot (page-map
// copy of shared page buffers — no data copied). Writes to a shared page
// deep-copy it on first touch (copy-on-write): cost proportional to the
// dirty page, never the table. rollback() restores the snapshot
// bit-exactly and truncates uncommitted journal entries.

#include "eafardb/table_fwd.hpp"
#include "eafardb/journal.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace eafardb {

class View; // materialized view (S5): automaton over page events

// --- Physical page: block-partitioned SoA storage (shared for COW) ---
struct PageData {
    // Local row index within the page (ascending-key-ordered map).
    std::map<int64_t, std::uint32_t> row_map;
    std::vector<int64_t> row_keys;
    // Per-type column arrays: [local column][local row].
    std::vector<std::vector<int64_t>> int_cols;
    std::vector<std::vector<double>> float_cols;

    std::size_t cell_count() const {
        return row_keys.size() * (int_cols.size() + float_cols.size());
    }
};

struct Page {
    std::shared_ptr<PageData> data;
    Page() : data(std::make_shared<PageData>()) {}
    // COW: ensure exclusive ownership before mutation.
    void detach(std::uint64_t& copied_cells) {
        if (data.use_count() > 1) {
            copied_cells += data->cell_count();
            data = std::make_shared<PageData>(*data);
        }
    }
};

class Table {
public:
    explicit Table(std::uint32_t page_size = 256);

    // --- Schema ---
    // Adds a column; returns its id. Throws on duplicate name.
    std::uint32_t add_column(std::string name, ColumnType type);
    // Throws std::out_of_range if no such column.
    std::uint32_t column_id(std::string_view name) const;
    ColumnType column_type(std::uint32_t col) const;
    std::size_t column_count() const { return column_names_.size(); }

    // --- CRUD by key ---
    // Inserts a row with default values (0) in all columns.
    // No-op if the key already exists.
    void insert(int64_t key);
    bool contains(int64_t key) const;
    // Removes the key. Throws std::out_of_range if absent.
    void erase(int64_t key);
    std::size_t row_count() const;

    // Reads/writes a cell. Throws std::out_of_range if key absent
    // or column id invalid. Values are stored bit-exact (no sanitization).
    int64_t get_i(int64_t key, std::uint32_t col) const;
    double get_f(int64_t key, std::uint32_t col) const;
    void set_i(int64_t key, std::uint32_t col, int64_t value);
    void set_f(int64_t key, std::uint32_t col, double value);

    // --- Per-page columnar (SoA) access ---
    // Rows currently materialized in a page.
    std::size_t page_rows(std::uint64_t page) const;
    // Contiguous array of one column within one page.
    const std::vector<int64_t>& page_column_i(std::uint64_t page,
                                              std::uint32_t col) const;
    const std::vector<double>& page_column_f(std::uint64_t page,
                                             std::uint32_t col) const;

    // --- Sparse pages (S2) ---
    // Page containing a key. Negative keys are mapped via uint64_t.
    std::uint64_t page_id(int64_t key) const { return static_cast<std::uint64_t>(key) / page_size_; }
    // Number of pages that currently hold at least one row (physical).
    std::uint64_t page_count() const { return static_cast<std::uint64_t>(pages_.size()); }
    // True if the page is currently materialized (holds at least one row).
    bool has_page(std::uint64_t page) const { return pages_.find(page) != pages_.end(); }
    // All materialized page ids (ascending). For view materialization.
    std::vector<std::uint64_t> materialized_pages() const {
        std::vector<std::uint64_t> ids;
        ids.reserve(pages_.size());
        for (const auto& [pid, page] : pages_) {
            ids.push_back(pid);
        }
        return ids;
    }
    // Pages touched by scans (monotonic counter, for the S2 thesis).
    std::uint64_t touched_pages() const { return touched_pages_; }
    // Cells deep-copied by copy-on-write (S4 counter proof).
    std::uint64_t copied_cells() const { return copied_cells_; }

    // S9: install the timestamp clock for FUTURE journal entries.
    // Determinism: tests drive the timeline explicitly.
    void set_journal_clock(Journal::Clock clock) { journal_.set_clock(std::move(clock)); }

    // --- Materialized views (S5): view = automaton over page events ---
    // A view subscribes via View's constructor (source table's attach()).
    // Every mutation that touches a page notifies attached views, so a
    // view can recompute only its dirty pages (incremental, lazy).
    void attach(View& view);

    // --- Transactions (S4) ---
    void begin_transaction();
    void commit();
    void rollback(); // bit-exact restore + journal truncation
    bool in_transaction() const { return snapshot_ != nullptr; }

    // --- Journal (S3) ---
    // The write journal of this table's mutations, in apply order.
    const Journal& journal() const { return journal_; }
    // Reconstructs a table from a journal, from empty. The result is
    // bit-identical to the table the journal was recorded from.
    static Table replay(const Journal& journal, std::uint32_t page_size = 256);
    // S9: time-travel — reconstructs the state as of timestamp `t`:
    // replays only entries with ts <= t (journal = historian).
    static Table replay_at(const Journal& journal, std::uint64_t t,
                           std::uint32_t page_size = 256);

    // Full scans, in ascending key order. Each distinct page visited
    // increments touched_pages_ exactly once. Callback receives (key, value).
    template <typename Fn>
    void scan_i(std::uint32_t col, Fn&& fn) const {
        scan_i_impl(col, false, 0, 0, std::forward<Fn>(fn));
    }
    template <typename Fn>
    void scan_f(std::uint32_t col, Fn&& fn) const {
        scan_f_impl(col, false, 0, 0, std::forward<Fn>(fn));
    }
    // Ranged scans over [from, to] inclusive (works with negative keys).
    template <typename Fn>
    void scan_range_i(std::uint32_t col, int64_t from, int64_t to, Fn&& fn) const {
        scan_i_impl(col, true, from, to, std::forward<Fn>(fn));
    }
    template <typename Fn>
    void scan_range_f(std::uint32_t col, int64_t from, int64_t to, Fn&& fn) const {
        scan_f_impl(col, true, from, to, std::forward<Fn>(fn));
    }

private:
    std::uint32_t row_of_page(const Page& p, int64_t key) const; // throws
    Page& mutable_page(std::uint64_t page_id);  // detach-on-write
    const Page& const_page(std::uint64_t page_id) const;
    // Local index of a column within its per-type array; throws if the
    // column's type does not match the requested access.
    std::uint32_t int_index(std::uint32_t col) const;
    std::uint32_t float_index(std::uint32_t col) const;

    void scan_i_impl(std::uint32_t col, bool ranged, int64_t from, int64_t to,
                     const std::function<void(int64_t, int64_t)>& fn) const;
    void scan_f_impl(std::uint32_t col, bool ranged, int64_t from, int64_t to,
                     const std::function<void(int64_t, double)>& fn) const;

    std::vector<std::string> column_names_;
    std::vector<ColumnType> column_types_;
    // Global column id -> local index inside int_cols/float_cols of a page.
    std::vector<std::uint32_t> type_index_;
    // Materialized pages: page_id -> page (ordered, deterministic).
    std::map<std::uint64_t, Page> pages_;

    std::uint32_t page_size_;          // keys per page
    mutable std::uint64_t touched_pages_ = 0; // scan page-visit counter
    std::uint64_t copied_cells_ = 0;   // COW deep-copy counter (S4)
    Journal journal_;                  // write journal (S3)

    // Subscribed materialized views (S5). Views are notified on every
    // page-touching mutation (insert/erase/set), and recompute lazily.
    std::vector<View*> views_;

    void notify_views(std::uint64_t page_id); // S5 page-event fan-out

    // Transaction state (S4).
    struct Snapshot {
        std::map<std::uint64_t, Page> pages;
        std::size_t journal_size = 0;
    };
    std::shared_ptr<Snapshot> snapshot_; // non-null inside a transaction
};

} // namespace eafardb
