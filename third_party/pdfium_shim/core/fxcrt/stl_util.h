#pragma once
#include <cstddef>
namespace fxcrt {
template <typename ResultType, typename Collection>
constexpr ResultType CollectionSize(const Collection& c) { return static_cast<ResultType>(c.size()); }
} // namespace fxcrt