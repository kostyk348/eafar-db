#include "eafardb/table.hpp"

#include <cstring>
#include <limits>
#include <sstream>

namespace eafardb {

namespace {

std::runtime_error key_error(int64_t key) {
    std::ostringstream os;
    os << "eafardb: key not found: " << key;
    return std::runtime_error(os.str());
}

std::runtime_error column_error(std::string_view name) {
    std::ostringstream os;
    os << "eafardb: no such column: " << name;
    return std::runtime_error(os.str());
}

} // namespace

Table::Table(std::uint32_t page_size) {
    (void)page_size; // page materialization arrives in S2
}

std::uint32_t Table::add_column(std::string name, ColumnType type) {
    for (std::size_t i = 0; i < column_names_.size(); ++i) {
        if (column_names_[i] == name) {
            throw std::runtime_error("eafardb: duplicate column: " + name);
        }
    }
    column_names_.push_back(std::move(name));
    column_types_.push_back(type);
    if (type == ColumnType::Int64) {
        type_index_.push_back(static_cast<std::uint32_t>(int_columns_.size()));
        int_columns_.emplace_back();
    } else {
        type_index_.push_back(static_cast<std::uint32_t>(float_columns_.size()));
        float_columns_.emplace_back();
    }
    return static_cast<std::uint32_t>(column_names_.size() - 1);
}

std::uint32_t Table::column_id(std::string_view name) const {
    for (std::size_t i = 0; i < column_names_.size(); ++i) {
        if (column_names_[i] == name) {
            return static_cast<std::uint32_t>(i);
        }
    }
    throw column_error(name);
}

ColumnType Table::column_type(std::uint32_t col) const {
    if (col >= column_types_.size()) {
        throw std::out_of_range("eafardb: invalid column id");
    }
    return column_types_[col];
}

std::uint32_t Table::int_index(std::uint32_t col) const {
    if (col >= column_types_.size() || column_types_[col] != ColumnType::Int64) {
        throw std::out_of_range("eafardb: invalid column id");
    }
    return type_index_[col];
}

std::uint32_t Table::float_index(std::uint32_t col) const {
    if (col >= column_types_.size() || column_types_[col] != ColumnType::Float64) {
        throw std::out_of_range("eafardb: invalid column id");
    }
    return type_index_[col];
}

std::uint32_t Table::row_of(int64_t key) const {
    auto it = row_map_.find(key);
    if (it == row_map_.end()) {
        throw key_error(key);
    }
    return it->second;
}

void Table::insert(int64_t key) {
    if (row_map_.contains(key)) {
        return; // no-op on existing key
    }
    const std::uint32_t row = static_cast<std::uint32_t>(row_keys_.size());
    row_map_.emplace(key, row);
    row_keys_.push_back(key);
    for (auto& col : int_columns_) {
        col.push_back(0);
    }
    for (auto& col : float_columns_) {
        col.push_back(0.0);
    }
}

bool Table::contains(int64_t key) const {
    return row_map_.contains(key);
}

void Table::erase(int64_t key) {
    auto it = row_map_.find(key);
    if (it == row_map_.end()) {
        throw key_error(key);
    }
    const std::uint32_t row = it->second;
    row_map_.erase(it);

    // Swap-with-last keeps every column array contiguous and dense
    // (true SoA, no tombstones). Deterministic: given the same op
    // sequence, the moved key is always the same.
    const std::uint32_t last = static_cast<std::uint32_t>(row_keys_.size() - 1);
    if (row != last) {
        const int64_t moved_key = row_keys_[last];
        row_keys_[row] = moved_key;
        row_map_[moved_key] = row;
        for (auto& col : int_columns_) {
            col[row] = col[last];
        }
        for (auto& col : float_columns_) {
            col[row] = col[last];
        }
    }
    row_keys_.pop_back();
    for (auto& col : int_columns_) {
        col.pop_back();
    }
    for (auto& col : float_columns_) {
        col.pop_back();
    }
}

int64_t Table::get_i(int64_t key, std::uint32_t col) const {
    return int_columns_[int_index(col)][row_of(key)];
}

double Table::get_f(int64_t key, std::uint32_t col) const {
    return float_columns_[float_index(col)][row_of(key)];
}

void Table::set_i(int64_t key, std::uint32_t col, int64_t value) {
    int_columns_[int_index(col)][row_of(key)] = value;
}

void Table::set_f(int64_t key, std::uint32_t col, double value) {
    float_columns_[float_index(col)][row_of(key)] = value;
}

const std::vector<int64_t>& Table::column_i(std::uint32_t col) const {
    return int_columns_[int_index(col)];
}

const std::vector<double>& Table::column_f(std::uint32_t col) const {
    return float_columns_[float_index(col)];
}

} // namespace eafardb
