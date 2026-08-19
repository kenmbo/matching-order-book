#pragma once

#include <cstddef>

namespace lob {

inline constexpr std::size_t kMaximumActiveOrders = std::size_t{1} << 17;
inline constexpr std::size_t kMaximumPriceLevelsPerSide = std::size_t{1} << 12;

static_assert(kMaximumActiveOrders == 131'072);
static_assert(kMaximumPriceLevelsPerSide == 4'096);

}  // namespace lob
