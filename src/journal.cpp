#include "eafardb/journal.hpp"

namespace eafardb {

void Journal::add_column(std::uint32_t id, std::string name, std::uint8_t type) {
    JournalEntry e;
    e.op = JournalOp::AddColumn;
    e.column = id;
    e.type = type;
    e.key = 0;
    e.value_i = 0;
    e.value_bits = 0;
    e.name = std::move(name);
    entries_.push_back(std::move(e));
}

void Journal::insert(int64_t key) {
    JournalEntry e;
    e.op = JournalOp::Insert;
    e.column = 0;
    e.type = 0;
    e.key = key;
    e.value_i = 0;
    e.value_bits = 0;
    entries_.push_back(std::move(e));
}

void Journal::erase(int64_t key) {
    JournalEntry e;
    e.op = JournalOp::Erase;
    e.column = 0;
    e.type = 0;
    e.key = key;
    e.value_i = 0;
    e.value_bits = 0;
    entries_.push_back(std::move(e));
}

void Journal::set_i(int64_t key, std::uint32_t column, int64_t value) {
    JournalEntry e;
    e.op = JournalOp::SetI;
    e.column = column;
    e.type = 0;
    e.key = key;
    e.value_i = value;
    e.value_bits = 0;
    entries_.push_back(std::move(e));
}

void Journal::set_f(int64_t key, std::uint32_t column, std::uint64_t value_bits) {
    JournalEntry e;
    e.op = JournalOp::SetF;
    e.column = column;
    e.type = 0;
    e.key = key;
    e.value_i = 0;
    e.value_bits = value_bits;
    entries_.push_back(std::move(e));
}

} // namespace eafardb
