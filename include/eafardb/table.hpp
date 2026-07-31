#pragma once
// eafardb/table.hpp — S1: Columnar table storage.
//
// The paradigm's "everything is a field" applied to a database:
// a table is a set of named fields (columns), each stored SoA
// (structure-of-arrays) — one contiguous array per column. A "row"
// is just an index across those arrays; there is no per-row object.
//
// Keys are int64. Storage is sparse-friendly: only inserted keys
// occupy rows; absent keys cost nothing (page materialization in S2).

#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace eafardb {

enum class ColumnType : std::uint8_t { Int64, Float64 };

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

private:
    std::uint32_t row_of(int64_t key) const; // throws if absent
    // Local index of a column within its per-type array; throws if the
    // column's type does not match the requested access.
    std::uint32_t int_index(std::uint32_t col) const;
    std::uint32_t float_index(std::uint32_t col) const;

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
};

} // namespace eafardb
