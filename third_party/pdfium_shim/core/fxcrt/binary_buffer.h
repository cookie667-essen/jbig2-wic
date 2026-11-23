#pragma once
#include "core/fxcrt/data_vector.h"
#include "core/fxcrt/span.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

class CFX_BinaryBuf {
    DataVector<uint8_t> buf_;
public:
    CFX_BinaryBuf() = default;
    ~CFX_BinaryBuf() = default;

    bool IsEmpty() const { return buf_.empty(); }
    size_t GetSize() const { return buf_.size(); }
    size_t GetLength() const { return buf_.size(); }
    pdfium::span<uint8_t> GetSpan() { return buf_; }
    pdfium::span<const uint8_t> GetSpan() const { return buf_; }

    void EstimateSize(size_t size) { buf_.reserve(size); }
    void Clear() { buf_.clear(); }

    void AppendByte(uint8_t byte) { buf_.push_back(byte); }
    void AppendSpan(pdfium::span<const uint8_t> sp) {
        buf_.insert(buf_.end(), sp.begin(), sp.end());
    }
    void AppendBlock(const void* data, size_t size) {
        if (!data || size == 0) return;
        const auto* p = static_cast<const uint8_t*>(data);
        buf_.insert(buf_.end(), p, p + size);
    }
    DataVector<uint8_t> DetachBuffer() {
        DataVector<uint8_t> out = std::move(buf_);
        buf_.clear();
        return out;
    }
};