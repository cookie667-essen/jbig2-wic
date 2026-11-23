#pragma once
#include "core/fxcrt/check.h"
#include "core/fxcrt/fx_safe_types.h"
#include <cstdint>
#include <optional>

namespace fxge {

inline std::optional<uint32_t> CalculatePitch8(uint32_t bpc, uint32_t components, int width) {
    if (width < 0) return std::nullopt;
    FX_SAFE_UINT32 pitch = bpc;
    pitch *= components;
    pitch *= static_cast<uint32_t>(width);
    pitch += 7u;
    if (!pitch.IsValid()) return std::nullopt;
    return pitch.ValueOrDie() / 8u;
}

inline std::optional<uint32_t> CalculatePitch32(int bpp, int width) {
    if (bpp < 0 || width < 0) return std::nullopt;
    FX_SAFE_UINT32 pitch = static_cast<uint32_t>(bpp);
    pitch *= static_cast<uint32_t>(width);
    pitch += 31u;
    if (!pitch.IsValid()) return std::nullopt;
    return (pitch.ValueOrDie() / 32u) * 4u;
}

inline uint32_t CalculatePitch8OrDie(uint32_t bpc, uint32_t components, int width) {
    auto res = CalculatePitch8(bpc, components, width);
    CHECK(res.has_value());
    return *res;
}

inline uint32_t CalculatePitch32OrDie(int bpp, int width) {
    auto res = CalculatePitch32(bpp, width);
    CHECK(res.has_value());
    return *res;
}

} // namespace fxge