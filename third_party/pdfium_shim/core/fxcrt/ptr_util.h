#pragma once
#include <memory>
namespace pdfium {
template <typename T>
std::unique_ptr<T> WrapUnique(T* ptr) { return std::unique_ptr<T>(ptr); }
} // namespace pdfium