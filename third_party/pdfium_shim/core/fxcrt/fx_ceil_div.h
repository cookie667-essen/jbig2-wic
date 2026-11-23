#pragma once
#include <type_traits>
namespace fxcrt {
template <typename T, typename U>
constexpr auto CeilDiv(T a, U b) {
    using R = std::common_type_t<T, U>;
    return (static_cast<R>(a) + static_cast<R>(b) - 1) / static_cast<R>(b);
}
} // namespace fxcrt