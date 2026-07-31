#include "eafardb/journal.hpp"

namespace eafardb {

namespace {

JournalEntry base_entry(JournalOp op, std::uint32_t column, std::uint8_t type,
                        int64_t key, int64_t value_i, std::uint64_t value_bits) {
    JournalEntry e;
    e.op = op;
    e.column = column;
    e.type = type;
    e.key = key;
    e.value_i = value_i;
    e.value_bits = value_bits;
    return e;
}

} // namespace

void Journal::add_column(std::uint32_t id, std::string name, std::uint8_t type) {
    JournalEntry e = base_entry(JournalOp::AddColumn, id, type, 0, 0, 0);
    e.name = std::move(name);
    stamp(e); // S9
    entries_.push_back(std::move(e));
}

void Journal::insert(int64_t key) {
    JournalEntry e = base_entry(JournalOp::Insert, 0, 0, key, 0, 0);
    stamp(e); // S9
    entries_.push_back(std::move(e));
}

void Journal::erase(int64_t key) {
    JournalEntry e = base_entry(JournalOp::Erase, 0, 0, key, 0, 0);
    stamp(e); // S9
    entries_.push_back(std::move(e));
}

void Journal::set_i(int64_t key, std::uint32_t column, int64_t value) {
    JournalEntry e = base_entry(JournalOp::SetI, column, 0, key, value, 0);
    stamp(e); // S9
    entries_.push_back(std::move(e));
}

void Journal::set_f(int64_t key, std::uint32_t column, std::uint64_t value_bits) {
    JournalEntry e = base_entry(JournalOp::SetF, column, 0, key, 0, value_bits);
    stamp(e); // S9
    entries_.push_back(std::move(e));
}

void Journal::begin_tx() {
    JournalEntry e = base_entry(JournalOp::BeginTx, 0, 0, 0, 0, 0);
    stamp(e); // S9
    entries_.push_back(std::move(e));
}

void Journal::commit_tx() {
    JournalEntry e = base_entry(JournalOp::CommitTx, 0, 0, 0, 0, 0);
    stamp(e); // S9
    entries_.push_back(std::move(e));
}

} // namespace eafardb
