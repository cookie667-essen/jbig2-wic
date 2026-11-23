#pragma once
#include "core/fxcrt/fx_safe_types.h"
inline size_t Fx2DSizeOrDie(size_t w, size_t h) {
    FX_SAFE_SIZE_T s = w; s *= h; return s.ValueOrDie();
}
inline size_t Fx2DSizeOrZero(size_t w, size_t h) {
    FX_SAFE_SIZE_T s = w; s *= h; return s.ValueOrDefault(0);
}