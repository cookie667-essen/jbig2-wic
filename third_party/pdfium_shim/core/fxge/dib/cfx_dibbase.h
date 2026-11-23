#pragma once
#include "core/fxcrt/span.h"
#include <cstdint>

class CFX_DIBBase {
public:
    virtual ~CFX_DIBBase() = default;
    int GetWidth() const { return 0; }
    int GetHeight() const { return 0; }
    int GetBPP() const { return 1; }
    pdfium::span<const uint8_t> GetScanline(int) const { return {}; }
};