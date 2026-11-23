#pragma once
#include "core/fxcrt/check.h"
#include "core/fxcrt/compiler_specific.h"
#include "core/fxcrt/data_vector.h"
#include "core/fxcrt/numerics/safe_conversions.h"
#include <algorithm>
#include <concepts>
#include <cstdint>
#include <iterator>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace pdfium {
inline constexpr size_t dynamic_extent = std::dynamic_extent;

template <typename T, size_t Extent = std::dynamic_extent>
class span : public std::span<T, Extent> {
    using Base = std::span<T, Extent>;
public:
    using Base::Base;
    constexpr span(Base b) noexcept : Base(b) {}

    template <typename U, size_t E>
        requires std::convertible_to<std::span<U, E>, Base>
    constexpr span(const span<U, E>& o) noexcept : Base(o.data(), o.size()) {}

    constexpr span<T> first(size_t n) const { return Base::first(n); }
    template <size_t N>
    constexpr span<T, N> first() const { return Base::template first<N>(); }

    constexpr span<T> last(size_t n) const { return Base::last(n); }
    template <size_t N>
    constexpr span<T, N> last() const { return Base::template last<N>(); }

    constexpr span<T> subspan(size_t off, size_t cnt = std::dynamic_extent) const { return Base::subspan(off, cnt); }
    template <size_t Off, size_t Cnt = std::dynamic_extent>
    constexpr auto subspan() const { return span<T, Cnt>(Base::template subspan<Off, Cnt>()); }

    constexpr std::pair<span<T>, span<T>> split_at(size_t n) const {
        return { first(n), subspan(n) };
    }
    constexpr T& take_first_elem() {
        T& elem = this->front();
        *this = subspan(1);
        return elem;
    }
    constexpr span<T> take_first(size_t n) {
        auto head = first(n);
        *this = subspan(n);
        return head;
    }
    template <size_t N>
    constexpr span<T, N> take_first() {
        auto head = first<N>();
        *this = subspan(N);
        return head;
    }
    template <typename U, size_t E>
    constexpr void copy_from(span<U, E> src) const {
        std::copy(src.begin(), src.end(), this->begin());
    }
    template <typename U, size_t E>
    constexpr void copy_prefix_from(span<U, E> src) const {
        std::copy(src.begin(), src.end(), this->begin());
    }
};

// CTAD Deduction Guides
template <typename T, size_t N>
span(T (&)[N]) -> span<T, N>;
template <typename T>
span(T*, size_t) -> span<T>;
template <typename It, typename End>
    requires std::contiguous_iterator<It>
span(It, End) -> span<std::remove_reference_t<std::iter_reference_t<It>>>;
template <typename R>
    requires std::ranges::contiguous_range<R>
span(R&&) -> span<std::remove_reference_t<std::ranges::range_reference_t<R>>>;

template <typename T>
using raw_span = span<T>;

template <typename T>
constexpr span<const uint8_t> as_byte_span(const T& c) {
    auto s = std::span(c);
    return { reinterpret_cast<const uint8_t*>(s.data()), s.size_bytes() };
}
template <typename T>
constexpr span<uint8_t> as_writable_byte_span(T& c) {
    auto s = std::span(c);
    return { reinterpret_cast<uint8_t*>(s.data()), s.size_bytes() };
}
template <typename T>
constexpr span<const T, 1> span_from_ref(const T& v) { return { &v, 1 }; }

} // namespace pdfium