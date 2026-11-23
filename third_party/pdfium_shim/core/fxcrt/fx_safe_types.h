#pragma once
#include "core/fxcrt/check.h"
#include "core/fxcrt/numerics/safe_conversions.h"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

template <typename T>
class CheckedNumeric {
    T val_ = 0;
    bool valid_ = true;
public:
    constexpr CheckedNumeric() = default;
    constexpr CheckedNumeric(T v) : val_(v), valid_(true) {}
    constexpr CheckedNumeric(T v, bool ok) : val_(v), valid_(ok) {}

    template <typename U>
        requires (std::is_integral_v<U> && !std::is_same_v<U, T>)
    constexpr CheckedNumeric(U v) {
        if (!std::in_range<T>(v)) { valid_ = false; return; }
        val_ = static_cast<T>(v);
        valid_ = true;
    }

    constexpr bool IsValid() const { return valid_; }
    T ValueOrDie() const {
        if (!valid_) throw jbig2::DecodeAbort("CheckedNumeric overflow");
        return val_;
    }
    constexpr T ValueOrDefault(T def) const { return valid_ ? val_ : def; }

    template <typename U>
    constexpr bool AssignIfValid(U* out) const {
        if (!valid_ || !std::in_range<U>(val_)) return false;
        *out = static_cast<U>(val_);
        return true;
    }

    constexpr CheckedNumeric operator-() const {
        if (!valid_) return CheckedNumeric(0, false);
        if constexpr (std::is_signed_v<T>) {
            if (val_ == std::numeric_limits<T>::min()) return CheckedNumeric(0, false);
            return CheckedNumeric(static_cast<T>(-val_), true);
        } else {
            return val_ == 0 ? CheckedNumeric(0, true) : CheckedNumeric(0, false);
        }
    }

    constexpr CheckedNumeric& operator+=(T rhs) {
        if (!valid_) return *this;
        if constexpr (std::is_unsigned_v<T>) {
            if (rhs > std::numeric_limits<T>::max() - val_) { valid_ = false; return *this; }
            val_ += rhs;
        } else {
            int64_t r = static_cast<int64_t>(val_) + static_cast<int64_t>(rhs);
            if (!std::in_range<T>(r)) { valid_ = false; return *this; }
            val_ = static_cast<T>(r);
        }
        return *this;
    }
    constexpr CheckedNumeric& operator+=(const CheckedNumeric& rhs) {
        if (!valid_ || !rhs.valid_) { valid_ = false; return *this; }
        return (*this += rhs.val_);
    }

    constexpr CheckedNumeric& operator-=(T rhs) {
        if (!valid_) return *this;
        if constexpr (std::is_unsigned_v<T>) {
            if (rhs > val_) { valid_ = false; return *this; }
            val_ -= rhs;
        } else {
            int64_t r = static_cast<int64_t>(val_) - static_cast<int64_t>(rhs);
            if (!std::in_range<T>(r)) { valid_ = false; return *this; }
            val_ = static_cast<T>(r);
        }
        return *this;
    }
    constexpr CheckedNumeric& operator-=(const CheckedNumeric& rhs) {
        if (!valid_ || !rhs.valid_) { valid_ = false; return *this; }
        return (*this -= rhs.val_);
    }

    constexpr CheckedNumeric& operator*=(T rhs) {
        if (!valid_) return *this;
        if (val_ == 0 || rhs == 0) { val_ = 0; return *this; }
        if constexpr (std::is_unsigned_v<T>) {
            if (rhs > std::numeric_limits<T>::max() / val_) { valid_ = false; return *this; }
            val_ *= rhs;
        } else {
            int64_t r = static_cast<int64_t>(val_) * static_cast<int64_t>(rhs);
            if (!std::in_range<T>(r)) { valid_ = false; return *this; }
            val_ = static_cast<T>(r);
        }
        return *this;
    }
    constexpr CheckedNumeric& operator*=(const CheckedNumeric& rhs) {
        if (!valid_ || !rhs.valid_) { valid_ = false; return *this; }
        return (*this *= rhs.val_);
    }

    constexpr CheckedNumeric& operator<<=(unsigned shift) {
        if (!valid_) return *this;
        if (shift >= sizeof(T) * 8) { valid_ = false; return *this; }
        if constexpr (std::is_unsigned_v<T>) {
            if (shift > 0 && (val_ >> (sizeof(T) * 8 - shift)) != 0) { valid_ = false; return *this; }
        }
        val_ = static_cast<T>(val_ << shift);
        return *this;
    }
    constexpr CheckedNumeric& operator>>=(unsigned shift) {
        if (!valid_) return *this;
        if (shift >= sizeof(T) * 8) { valid_ = false; return *this; }
        val_ = static_cast<T>(val_ >> shift);
        return *this;
    }
    constexpr CheckedNumeric& operator|=(T rhs) { if (valid_) val_ |= rhs; return *this; }
    constexpr CheckedNumeric& operator&=(T rhs) { if (valid_) val_ &= rhs; return *this; }

    friend constexpr CheckedNumeric operator+(CheckedNumeric a, T b) { return a += b; }
    friend constexpr CheckedNumeric operator+(CheckedNumeric a, const CheckedNumeric& b) { return a += b; }
    friend constexpr CheckedNumeric operator-(CheckedNumeric a, T b) { return a -= b; }
    friend constexpr CheckedNumeric operator-(CheckedNumeric a, const CheckedNumeric& b) { return a -= b; }
    friend constexpr CheckedNumeric operator*(CheckedNumeric a, T b) { return a *= b; }
    friend constexpr CheckedNumeric operator*(CheckedNumeric a, const CheckedNumeric& b) { return a *= b; }
};

using FX_SAFE_UINT32 = CheckedNumeric<uint32_t>;
using FX_SAFE_INT32  = CheckedNumeric<int32_t>;
using FX_SAFE_SIZE_T = CheckedNumeric<size_t>;

template <size_t Boundary, typename T>
constexpr T FxAlignToBoundary(T val) {
    return static_cast<T>(((val + Boundary - 1) / Boundary) * Boundary);
}