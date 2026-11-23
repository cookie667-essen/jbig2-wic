#pragma once
#include <algorithm>
#include <limits>
namespace pdfium {
template <typename Dst, typename Src>
constexpr Dst checked_cast(Src v) { return static_cast<Dst>(v); }
template <typename Dst, typename Src>
constexpr Dst saturated_cast(Src v) {
    if (v < static_cast<Src>(std::numeric_limits<Dst>::min())) return std::numeric_limits<Dst>::min();
    if (v > static_cast<Src>(std::numeric_limits<Dst>::max())) return std::numeric_limits<Dst>::max();
    return static_cast<Dst>(v);
}
} // namespace pdfium