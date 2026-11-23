#pragma once
#include "core/fxcrt/data_vector.h"
#include "core/fxcrt/fixed_size_data_vector.h"
#include "core/fxcrt/span.h"
#include <cstddef>
#include <utility>

template <typename T>
class MaybeOwnedDataVector {
    DataVector<T> owned_;
    pdfium::span<T> view_;
    bool is_owned_ = false;
public:
    MaybeOwnedDataVector() = default;
    MaybeOwnedDataVector(pdfium::span<T> s) : view_(s), is_owned_(false) {}
    MaybeOwnedDataVector(DataVector<T> v) : owned_(std::move(v)), view_(owned_), is_owned_(true) {}
    MaybeOwnedDataVector(FixedSizeDataVector<T> v)
        : owned_(v.begin(), v.end()), view_(owned_), is_owned_(true) {}

    MaybeOwnedDataVector(const MaybeOwnedDataVector& o)
        : owned_(o.owned_),
          view_(o.is_owned_ ? pdfium::span<T>(owned_) : o.view_),
          is_owned_(o.is_owned_) {}

    MaybeOwnedDataVector& operator=(const MaybeOwnedDataVector& o) {
        if (this != &o) {
            owned_ = o.owned_;
            is_owned_ = o.is_owned_;
            view_ = is_owned_ ? pdfium::span<T>(owned_) : o.view_;
        }
        return *this;
    }

    MaybeOwnedDataVector(MaybeOwnedDataVector&& o) noexcept
        : owned_(std::move(o.owned_)),
          view_(o.is_owned_ ? pdfium::span<T>(owned_) : o.view_),
          is_owned_(o.is_owned_) {
        o.view_ = {};
        o.is_owned_ = false;
    }

    MaybeOwnedDataVector& operator=(MaybeOwnedDataVector&& o) noexcept {
        if (this != &o) {
            owned_ = std::move(o.owned_);
            is_owned_ = o.is_owned_;
            view_ = is_owned_ ? pdfium::span<T>(owned_) : o.view_;
            o.view_ = {};
            o.is_owned_ = false;
        }
        return *this;
    }

    bool IsOwned() const { return is_owned_; }
    void ResizeOwned(size_t new_size, T fill = T{}) {
        owned_.resize(new_size, fill);
        view_ = owned_;
        is_owned_ = true;
    }
    bool empty() const { return view_.empty(); }
    size_t size() const { return view_.size(); }
    pdfium::span<T> span() { return view_; }
    pdfium::span<const T> span() const { return view_; }
    pdfium::span<T> writable_span() { return view_; }
    T& operator[](size_t i) { return view_[i]; }
    const T& operator[](size_t i) const { return view_[i]; }
};