#pragma once
// eafardb/table.hpp — S1/S2/S3: Columnar table storage, sparse pages,
// write journal + determinism.
//
// The paradigm's "everything is a field" applied to a database:
// a table is a set of named fields (columns), each stored SoA
// (structure-of-arrays) — one contiguous array per column. A "row"
// is just an index across those arrays; there is no per-row object.
//
// Keys are int64. Storage is sparse: only inserted/written keys occupy
// rows; absent keys cost nothing. Pages are a *logical* grouping of keys
// (page = key / page_size). A page with no rows does not exist physically
// (zero memory) — it "wakes up" on first write. Scans iterate only rows
// that exist and count distinct pages touched: scan cost is proportional
// to materialized pages, never to the key range (S2 thesis).
//
// Every mutation is recorded in a write journal (S3). The journal is the
// primary source of truth: Table::replay() reconstructs bit-identical
// state (including NaN bit patterns and -0.0) from an empty table.

#include "eafardb/table_fwd.hpp"
#include "eafardb/journal.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace eafardb {

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
    std::size_t row_count() const { return row_keys_.size(); }

    // Reads/writes a cell. Throws std::out_of_range if key absent
    // or column id invalid. Values are stored bit-exact (no sanitization).
    int64_t get_i(int64_t key, std::uint32_t col) const;
    double get_f(int64_t key, std::uint32_t col) const;
    void set_i(int64_t key, std::uint32_t col, int64_t value);
    void set_f(int64_t key, std::uint32_t col, double value);

    // --- Column-wise (SoA) access ---
    // Contiguous array of all values in this column, in row order.
    const std::vector<int64_t>& column_i(std::uint32_t col) const;
    const std::vector<double>& column_f(std::uint32_t col) const;

    // --- Sparse pages (S2) ---
    // Page containing a key. Negative keys are mapped via uint64_t.
    std::uint64_t page_id(int64_t key) const { return static_cast<std::uint64_t>(key) / page_size_; }
    // Number of distinct pages that currently hold at least one row.
    // A page that held rows but has none left is gone (sleeping).
    std::uint64_t page_count() const;
    // Pages touched by scans (monotonic counter, for the S2 thesis).
    std::uint64_t touched_pages() const { return touched_pages_; }

    // --- Journal (S3) ---
    // The write journal of this table's mutations, in apply order.
    const Journal& journal() const { return journal_; }
    // Reconstructs a table from a journal, from empty. The result is
    // bit-identical to the table the journal was recorded from.
    static Table replay(const Journal& journal, std::uint32_t page_size = 256);

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
    std::uint32_t row_of(int64_t key) const; // throws if absent
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
    // Global column id -> local index inside int_columns_/float_columns_.
    std::vector<std::uint32_t> type_index_;
    // key -> dense row index (into the column arrays below).
    std::map<int64_t, std::uint32_t> row_map_;
    std::vector<int64_t> row_keys_; // for row_count and future scans
    // SoA storage: one contiguous array per column (per type).
    std::vector<std::vector<int64_t>> int_columns_;
    std::vector<std::vector<double>> float_columns_;

    std::uint32_t page_size_;      // keys per page
    mutable std::uint64_t touched_pages_ = 0; // scan page-visit counter
    Journal journal_;              // write journal of this table (S3)
};

} // namespace eafardb
