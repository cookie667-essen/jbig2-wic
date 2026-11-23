#pragma once
#include <algorithm>
#include <cstddef>
#include <tuple>
#include <span>

namespace fxcrt {
template <typename... Ranges>
class ZipView {
    std::tuple<Ranges...> ranges_;
    size_t len_;
public:
    explicit ZipView(Ranges... rs)
        : ranges_(rs...), len_(std::min({ std::size(rs)... })) {}

    struct Iterator {
        ZipView* parent;
        size_t idx;
        bool operator!=(const Iterator& o) const { return idx != o.idx; }
        Iterator& operator++() { ++idx; return *this; }
        auto operator*() const {
            return std::apply([&](auto&... rs) { return std::tie(rs[idx]...); }, parent->ranges_);
        }
    };
    Iterator begin() { return { this, 0 }; }
    Iterator end()   { return { this, len_ }; }
};

template <typename... Ranges>
auto Zip(Ranges&&... rs) {
    return ZipView<decltype(std::span(rs))...>(std::span(rs)...);
}
} // namespace fxcrt