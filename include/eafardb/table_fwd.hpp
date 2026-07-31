#pragma once
// eafardb/table_fwd.hpp — forward declarations shared by journal and table.

#include <cstdint>

namespace eafardb {

enum class ColumnType : std::uint8_t { Int64 = 0, Float64 = 1 };

} // namespace eafardb
