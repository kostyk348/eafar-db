#pragma once
// eafardb/journal.hpp — S3: Append-only write journal.
//
// Paradigm: the journal is the primary source of truth; the table state
// is derived from it. Replaying the journal from an empty table
// reproduces the state bit-exactly (including NaN bit patterns and -0.0).
//
// The journal is self-contained: schema operations (add_column) and data
// operations (insert/erase/set) are recorded in the same order they were
// applied, so replay needs nothing but the journal itself.

#include "eafardb/table_fwd.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace eafardb {

enum class JournalOp : std::uint8_t {
    AddColumn = 0,
    Insert = 1,
    Erase = 2,
    SetI = 3,
    SetF = 4,
};

struct JournalEntry {
    JournalOp op;
    std::uint32_t column;     // AddColumn: assigned id; SetI/SetF: column id
    std::uint8_t type;        // AddColumn: ColumnType as uint8
    int64_t key;              // Insert/Erase/SetI/SetF
    int64_t value_i;          // SetI value
    std::uint64_t value_bits; // SetF value, bit-exact (memcpy of double)
    std::string name;         // AddColumn: column name
};

class Journal {
public:
    Journal() = default;

    void add_column(std::uint32_t id, std::string name, std::uint8_t type);
    void insert(int64_t key);
    void erase(int64_t key);
    void set_i(int64_t key, std::uint32_t column, int64_t value);
    void set_f(int64_t key, std::uint32_t column, std::uint64_t value_bits);

    std::size_t size() const { return entries_.size(); }
    const std::vector<JournalEntry>& entries() const { return entries_; }
    // Drops entries from position `keep` onward (rollback of uncommitted
    // work: an aborted transaction is not history).
    void truncate(std::size_t keep) {
        if (keep > entries_.size()) {
            keep = entries_.size();
        }
        entries_.resize(keep);
    }

private:
    std::vector<JournalEntry> entries_;
};

} // namespace eafardb
