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

#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace eafardb {

enum class JournalOp : std::uint8_t {
    AddColumn = 0,
    Insert = 1,
    Erase = 2,
    SetI = 3,
    SetF = 4,
    BeginTx = 5,  // S8: transaction boundary markers (audit, not replayed)
    CommitTx = 6, // S8: end of a committed transaction
};

struct JournalEntry {
    JournalOp op;
    std::uint32_t column;     // AddColumn: assigned id; SetI/SetF: column id
    std::uint8_t type;        // AddColumn: ColumnType as uint8
    int64_t key;              // Insert/Erase/SetI/SetF
    int64_t value_i;          // SetI value
    std::uint64_t value_bits; // SetF value, bit-exact (memcpy of double)
    std::string name;         // AddColumn: column name
    std::uint64_t ts = 0;     // S9: monotonic timestamp at append time
};

class Journal {
public:
    // Monotonic clock source. Default: steady milliseconds since epoch.
    // Injectable for determinism (tests drive the timeline explicitly).
    using Clock = std::function<std::uint64_t()>;

    Journal() : clock_(default_clock()) {}
    explicit Journal(Clock clock) : clock_(std::move(clock)) {}

    // Replaces the clock (e.g. install a deterministic one). Only affects
    // timestamps of FUTURE entries; history keeps its ts values.
    void set_clock(Clock clock) { clock_ = std::move(clock); }
    std::uint64_t now() const { return clock_(); }

    void add_column(std::uint32_t id, std::string name, std::uint8_t type);
    void insert(int64_t key);
    void erase(int64_t key);
    void set_i(int64_t key, std::uint32_t column, int64_t value);
    void set_f(int64_t key, std::uint32_t column, std::uint64_t value_bits);
    // S8: transaction boundary markers (audit; no-op on replay).
    void begin_tx();
    void commit_tx();

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

    // S8 audit: half-open index ranges [begin, end) of every committed
    // transaction (BeginTx..CommitTx inclusive), in commit order.
    // Entries recorded outside any transaction are not part of any range.
    std::vector<std::pair<std::size_t, std::size_t>> transaction_ranges() const {
        std::vector<std::pair<std::size_t, std::size_t>> out;
        std::size_t start = std::numeric_limits<std::size_t>::max();
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            switch (entries_[i].op) {
            case JournalOp::BeginTx:
                start = i;
                break;
            case JournalOp::CommitTx:
                if (start != std::numeric_limits<std::size_t>::max()) {
                    out.emplace_back(start, i + 1);
                }
                start = std::numeric_limits<std::size_t>::max();
                break;
            default:
                break;
            }
        }
        return out;
    }

private:
    std::vector<JournalEntry> entries_;
    Clock clock_;

    static Clock default_clock() {
        return [] {
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count());
        };
    }

    void stamp(JournalEntry& e) const { e.ts = clock_(); }
};

} // namespace eafardb
