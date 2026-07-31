#include "eafardb/table.hpp"
#include "eafardb/view.hpp"

#include <cstring>
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

Table::Table(std::uint32_t page_size) : page_size_(page_size > 0 ? page_size : 256) {}

std::uint32_t Table::add_column(std::string name, ColumnType type) {
    for (std::size_t i = 0; i < column_names_.size(); ++i) {
        if (column_names_[i] == name) {
            throw std::runtime_error("eafardb: duplicate column: " + name);
        }
    }
    column_names_.push_back(std::move(name));
    column_types_.push_back(type);
    // Local per-type index = number of same-type columns already present.
    std::uint32_t local = 0;
    for (std::size_t i = 0; i + 1 < column_types_.size(); ++i) {
        if (column_types_[i] == type) {
            ++local;
        }
    }
    type_index_.push_back(local);
    // Every materialized page gets a new column array for this column
    // (COW: never mutate a page shared with an open snapshot).
    if (type == ColumnType::Int64) {
        for (auto& [id, page] : pages_) {
            page.detach(copied_cells_);
            page.data->int_cols.emplace_back();
        }
    } else {
        for (auto& [id, page] : pages_) {
            page.detach(copied_cells_);
            page.data->float_cols.emplace_back();
        }
    }
    const std::uint32_t id = static_cast<std::uint32_t>(column_names_.size() - 1);
    journal_.add_column(id, column_names_.back(), static_cast<std::uint8_t>(type));
    return id;
}

void Table::attach(View& view) {
    views_.push_back(&view);
}

void Table::notify_views(std::uint64_t page_id) {
    for (View* v : views_) {
        v->on_page_written(page_id);
    }
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

const Page& Table::const_page(std::uint64_t id) const {
    auto it = pages_.find(id);
    if (it == pages_.end()) {
        throw std::out_of_range("eafardb: page not materialized");
    }
    return it->second;
}

Page& Table::mutable_page(std::uint64_t id) {
    auto it = pages_.find(id);
    if (it == pages_.end()) {
        throw std::out_of_range("eafardb: page not materialized");
    }
    it->second.detach(copied_cells_);
    return it->second;
}

std::uint32_t Table::row_of_page(const Page& p, int64_t key) const {
    auto it = p.data->row_map.find(key);
    if (it == p.data->row_map.end()) {
        throw key_error(key);
    }
    return it->second;
}

void Table::insert(int64_t key) {
    const std::uint64_t pid = page_id(key);
    auto it = pages_.find(pid);
    if (it != pages_.end() && it->second.data->row_map.contains(key)) {
        return; // no-op on existing key (not journaled: no state change)
    }
    if (it == pages_.end()) {
        it = pages_.emplace(pid, Page{}).first;
        // A fresh page must materialize arrays for every column that
        // already exists (add_column before insert / replay order).
        PageData& fresh = *it->second.data;
        for (const ColumnType t : column_types_) {
            if (t == ColumnType::Int64) {
                fresh.int_cols.emplace_back();
            } else {
                fresh.float_cols.emplace_back();
            }
        }
    } else {
        it->second.detach(copied_cells_);
    }
    PageData& d = *it->second.data;
    const std::uint32_t row = static_cast<std::uint32_t>(d.row_keys.size());
    d.row_map.emplace(key, row);
    d.row_keys.push_back(key);
    for (auto& col : d.int_cols) {
        col.push_back(0);
    }
    for (auto& col : d.float_cols) {
        col.push_back(0.0);
    }
    journal_.insert(key);
    notify_views(pid);
}

bool Table::contains(int64_t key) const {
    const auto it = pages_.find(page_id(key));
    return it != pages_.end() && it->second.data->row_map.contains(key);
}

void Table::erase(int64_t key) {
    const std::uint64_t pid = page_id(key);
    auto it = pages_.find(pid);
    if (it == pages_.end() || !it->second.data->row_map.contains(key)) {
        throw key_error(key);
    }
    it->second.detach(copied_cells_);
    PageData& d = *it->second.data;

    const std::uint32_t row = d.row_map.at(key);
    d.row_map.erase(key);

    // Swap-with-last keeps every column array contiguous and dense
    // (true SoA, no tombstones). Deterministic: given the same op
    // sequence, the moved key is always the same.
    const std::uint32_t last = static_cast<std::uint32_t>(d.row_keys.size() - 1);
    if (row != last) {
        const int64_t moved_key = d.row_keys[last];
        d.row_keys[row] = moved_key;
        d.row_map[moved_key] = row;
        for (auto& col : d.int_cols) {
            col[row] = col[last];
        }
        for (auto& col : d.float_cols) {
            col[row] = col[last];
        }
    }
    d.row_keys.pop_back();
    for (auto& col : d.int_cols) {
        col.pop_back();
    }
    for (auto& col : d.float_cols) {
        col.pop_back();
    }

    // Page went empty -> it sleeps (zero memory) again.
    if (d.row_keys.empty()) {
        pages_.erase(it);
    }
    journal_.erase(key);
    notify_views(pid);
}

std::size_t Table::row_count() const {
    std::size_t n = 0;
    for (const auto& [id, page] : pages_) {
        n += page.data->row_keys.size();
    }
    return n;
}

int64_t Table::get_i(int64_t key, std::uint32_t col) const {
    const auto& p = const_page(page_id(key));
    return p.data->int_cols[int_index(col)][row_of_page(p, key)];
}

double Table::get_f(int64_t key, std::uint32_t col) const {
    const auto& p = const_page(page_id(key));
    return p.data->float_cols[float_index(col)][row_of_page(p, key)];
}

void Table::set_i(int64_t key, std::uint32_t col, int64_t value) {
    auto& p = mutable_page(page_id(key));
    p.data->int_cols[int_index(col)][row_of_page(p, key)] = value;
    journal_.set_i(key, col, value);
    notify_views(page_id(key));
}

void Table::set_f(int64_t key, std::uint32_t col, double value) {
    auto& p = mutable_page(page_id(key));
    p.data->float_cols[float_index(col)][row_of_page(p, key)] = value;
    std::uint64_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    journal_.set_f(key, col, bits);
    notify_views(page_id(key));
}

std::size_t Table::page_rows(std::uint64_t page) const {
    return const_page(page).data->row_keys.size();
}

const std::vector<int64_t>& Table::page_column_i(std::uint64_t page, std::uint32_t col) const {
    return const_page(page).data->int_cols[int_index(col)];
}

const std::vector<double>& Table::page_column_f(std::uint64_t page, std::uint32_t col) const {
    return const_page(page).data->float_cols[float_index(col)];
}

// ---------------------------------------------------------------------------
// Transactions (S4) — snapshot/rollback with copy-on-write
// ---------------------------------------------------------------------------

void Table::begin_transaction() {
    if (snapshot_) {
        throw std::runtime_error("eafardb: transaction already open");
    }
    auto snap = std::make_shared<Snapshot>();
    snap->pages = pages_;             // shared_ptr copies: no data copied
    snap->journal_size = journal_.size();
    snapshot_ = std::move(snap);
    journal_.begin_tx(); // S8: audit marker (erased by rollback truncate)
}

void Table::commit() {
    if (!snapshot_) {
        throw std::runtime_error("eafardb: no open transaction");
    }
    journal_.commit_tx(); // S8: audit marker
    snapshot_.reset(); // COW'd pages keep their copies; state stays
}

void Table::rollback() {
    if (!snapshot_) {
        throw std::runtime_error("eafardb: no open transaction");
    }
    // Restore the page map bit-exactly (shared buffers from snapshot time).
    pages_ = snapshot_->pages;
    // Uncommitted journal entries are not history: truncate them.
    journal_.truncate(snapshot_->journal_size);
    snapshot_.reset();
}

// ---------------------------------------------------------------------------
// Journal replay (S3)
// ---------------------------------------------------------------------------

Table Table::replay(const Journal& journal, std::uint32_t page_size) {
    Table t(page_size);
    for (const auto& e : journal.entries()) {
        switch (e.op) {
        case JournalOp::AddColumn:
            t.add_column(e.name, static_cast<ColumnType>(e.type));
            break;
        case JournalOp::Insert:
            t.insert(e.key);
            break;
        case JournalOp::Erase:
            t.erase(e.key);
            break;
        case JournalOp::SetI:
            t.set_i(e.key, e.column, e.value_i);
            break;
        case JournalOp::SetF: {
            double v;
            std::memcpy(&v, &e.value_bits, sizeof(v));
            t.set_f(e.key, e.column, v);
            break;
        }
        // S8: transaction boundaries are audit metadata; replaying the
        // committed stream applies the same ops without them.
        case JournalOp::BeginTx:
        case JournalOp::CommitTx:
            break;
        }
    }
    return t;
}

// S9: time-travel replay — apply only entries stamped at or before `t`.
Table Table::replay_at(const Journal& journal, std::uint64_t t,
                       std::uint32_t page_size) {
    Table out(page_size);
    for (const auto& e : journal.entries()) {
        if (e.ts > t) {
            continue; // future history: not yet happened at time t
        }
        switch (e.op) {
        case JournalOp::AddColumn:
            out.add_column(e.name, static_cast<ColumnType>(e.type));
            break;
        case JournalOp::Insert:
            out.insert(e.key);
            break;
        case JournalOp::Erase:
            out.erase(e.key);
            break;
        case JournalOp::SetI:
            out.set_i(e.key, e.column, e.value_i);
            break;
        case JournalOp::SetF: {
            double v;
            std::memcpy(&v, &e.value_bits, sizeof(v));
            out.set_f(e.key, e.column, v);
            break;
        }
        case JournalOp::BeginTx:
        case JournalOp::CommitTx:
            break;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Scans (S2) — ascending pages, ascending keys within pages
// ---------------------------------------------------------------------------

void Table::scan_i_impl(std::uint32_t col, bool ranged, int64_t from, int64_t to,
                        const std::function<void(int64_t, int64_t)>& fn) const {
    const std::uint32_t local = int_index(col);
    std::uint64_t last_page = 0;
    bool first = true;
    for (const auto& [pid, page] : pages_) {
        const auto& d = *page.data;
        const auto& data = d.int_cols[local];
        // Page touched (distinct page visit) only if it yields >=1 row.
        bool page_touched = false;
        for (auto it = d.row_map.begin(); it != d.row_map.end(); ++it) {
            if (ranged && (it->first < from || it->first > to)) {
                continue;
            }
            if (!page_touched) {
                page_touched = true;
                if (first || pid != last_page) {
                    ++touched_pages_;
                    last_page = pid;
                    first = false;
                }
            }
            fn(it->first, data[it->second]);
        }
    }
}

void Table::scan_f_impl(std::uint32_t col, bool ranged, int64_t from, int64_t to,
                        const std::function<void(int64_t, double)>& fn) const {
    const std::uint32_t local = float_index(col);
    for (const auto& [pid, page] : pages_) {
        const auto& d = *page.data;
        const auto& data = d.float_cols[local];
        bool page_touched = false;
        for (auto it = d.row_map.begin(); it != d.row_map.end(); ++it) {
            if (ranged && (it->first < from || it->first > to)) {
                continue;
            }
            if (!page_touched) {
                page_touched = true;
                ++touched_pages_;
            }
            fn(it->first, data[it->second]);
        }
    }
}

} // namespace eafardb
