#pragma once
#include "core/fxcrt/span.h"
#include <algorithm>
#include <cstddef>
#include <span>

namespace fxcrt {

template <typename T, typename U>
void spancpy(pdfium::span<T> dst, pdfium::span<U> src) {
    std::copy(src.begin(), src.end(), dst.begin());
}

template <typename T, typename U>
void spanmove(pdfium::span<T> dst, pdfium::span<U> src) {
    std::move(src.begin(), src.end(), dst.begin());
}

template <typename T, typename U>
void Copy(pdfium::span<U> src, pdfium::span<T> dst) {
    spancpy(dst, src);
}

template <typename Dst, typename Container>
pdfium::span<Dst> reinterpret_span(Container&& c) {
    auto s = std::span(c);
    return pdfium::span<Dst>(
        reinterpret_cast<Dst*>(s.data()),
        s.size_bytes() / sizeof(Dst));
}

} // namespace fxcrt