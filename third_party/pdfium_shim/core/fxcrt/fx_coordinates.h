#pragma once
#include <algorithm>
#include <cstdint>
struct FX_RECT {
    int32_t left = 0;
    int32_t top = 0;
    int32_t right = 0;
    int32_t bottom = 0;
    constexpr FX_RECT() = default;
    constexpr FX_RECT(int32_t l, int32_t t, int32_t r, int32_t b) : left(l), top(t), right(r), bottom(b) {}
    constexpr int32_t Width() const { return right - left; }
    constexpr int32_t Height() const { return bottom - top; }
    constexpr bool IsEmpty() const { return right <= left || bottom <= top; }
    void Intersect(const FX_RECT& o) {
        left = std::max(left, o.left); top = std::max(top, o.top);
        right = std::min(right, o.right); bottom = std::min(bottom, o.bottom);
    }
    void Union(const FX_RECT& o) {
        if (o.IsEmpty()) return;
        if (IsEmpty()) { *this = o; return; }
        left = std::min(left, o.left); top = std::min(top, o.top);
        right = std::max(right, o.right); bottom = std::max(bottom, o.bottom);
    }
};