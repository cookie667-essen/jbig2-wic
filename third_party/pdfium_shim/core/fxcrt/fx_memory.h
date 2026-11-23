#pragma once
#include "core/fxcrt/data_vector.h"
#include "core/fxcrt/fx_safe_types.h"
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <memory>

template <typename T> T* FX_Alloc(size_t n) { return static_cast<T*>(std::calloc(n, sizeof(T))); }
template <typename T> T* FX_TryAlloc(size_t n) { return static_cast<T*>(std::calloc(n, sizeof(T))); }
inline void FX_Free(void* p) { std::free(p); }