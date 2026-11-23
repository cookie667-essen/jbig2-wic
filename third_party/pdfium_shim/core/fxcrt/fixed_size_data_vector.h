#pragma once
#include "core/fxcrt/span.h"
#include <cstddef>
#include <vector>

template <typename T>
class FixedSizeDataVector {
    std::vector<T> data_;
public:
    FixedSizeDataVector() = default;
    explicit FixedSizeDataVector(size_t n) : data_(n) {}
    static FixedSizeDataVector Zeroed(size_t n) { return FixedSizeDataVector(n); }
    static FixedSizeDataVector Uninit(size_t n) { return FixedSizeDataVector(n); }
    static FixedSizeDataVector TryZeroed(size_t n) { return FixedSizeDataVector(n); }
    static FixedSizeDataVector TryUninit(size_t n) { return FixedSizeDataVector(n); }

    bool empty() const { return data_.empty(); }
    size_t size() const { return data_.size(); }
    pdfium::span<const T> span() const { return data_; }
    pdfium::span<T> writable_span() { return data_; }
    pdfium::span<const T> subspan(size_t off, size_t cnt = std::dynamic_extent) const { return span().subspan(off, cnt); }
    pdfium::span<T> subspan(size_t off, size_t cnt = std::dynamic_extent) { return writable_span().subspan(off, cnt); }
    T& operator[](size_t i) { return data_[i]; }
    const T& operator[](size_t i) const { return data_[i]; }
    auto begin() { return data_.begin(); }
    auto end() { return data_.end(); }
    auto begin() const { return data_.begin(); }
    auto end() const { return data_.end(); }
};