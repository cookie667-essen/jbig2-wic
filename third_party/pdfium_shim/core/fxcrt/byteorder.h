#pragma once
#include "core/fxcrt/span.h"
#include <cstdint>

namespace fxcrt {
constexpr uint16_t FromBE16(uint16_t v) {
    return static_cast<uint16_t>((v << 8) | (v >> 8));
}
constexpr uint16_t ToBE16(uint16_t v) { return FromBE16(v); }

constexpr uint32_t FromBE32(uint32_t v) {
    return ((v & 0x000000FFu) << 24) | ((v & 0x0000FF00u) << 8) |
           ((v & 0x00FF0000u) >> 8)  | ((v & 0xFF000000u) >> 24);
}
constexpr uint32_t ToBE32(uint32_t v) { return FromBE32(v); }

inline uint16_t GetUInt16MSBFirst(pdfium::span<const uint8_t> s) {
    return static_cast<uint16_t>((uint16_t(s[0]) << 8) | uint16_t(s[1]));
}
inline uint32_t GetUInt32MSBFirst(pdfium::span<const uint8_t> s) {
    return (uint32_t(s[0]) << 24) | (uint32_t(s[1]) << 16) | (uint32_t(s[2]) << 8) | uint32_t(s[3]);
}
} // namespace fxcrt