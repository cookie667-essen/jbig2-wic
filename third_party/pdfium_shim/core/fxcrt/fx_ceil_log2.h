#pragma once
#include <bit>
#include <cstdint>
namespace fxcrt {
constexpr uint32_t CeilLog2(uint32_t n) {
    return n <= 1 ? 0 : static_cast<uint32_t>(std::bit_width(n - 1));
}
} // namespace fxcrt