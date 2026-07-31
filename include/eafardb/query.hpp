#pragma once
// eafardb/query.hpp вЂ” S7: fuzzy query (paradigm: fuzziness in the DB).
//
// A fuzzy query filters rows where membership(field, concept_name) >= threshold.
// The membership function is a named entry in the core library's
// FuzzyRegistry вЂ” swapped at runtime WITHOUT touching query code
// (automata depend on names, not on function shapes).
//
// Fuzzy is query-time only: no storage changes, no new index. The filter
// walks the source column (SoA scan) and applies the registry.

#include "eafardb/table.hpp"

#include <eafar/fuzzy.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace eafardb {

// Filter over one float column of a table.
class FuzzyQuery {
public:
    explicit FuzzyQuery(eafar::FuzzyRegistry& registry) : registry_(registry) {}

    // Keys (ascending) whose membership(concept_name, value) >= threshold.
    // Throws std::out_of_range if the concept_name is not defined.
    std::vector<int64_t> filter(const Table& table, std::uint32_t column,
                                const std::string& concept_name, float threshold) const {
        std::vector<int64_t> out;
        table.scan_f(column, [&](int64_t key, double value) {
            if (registry_.apply(concept_name, static_cast<float>(value)) >= threshold) {
                out.push_back(key);
            }
        });
        return out;
    }

    std::size_t count(const Table& table, std::uint32_t column,
                      const std::string& concept_name, float threshold) const {
        return filter(table, column, concept_name, threshold).size();
    }

private:
    eafar::FuzzyRegistry& registry_;
};

} // namespace eafardb
