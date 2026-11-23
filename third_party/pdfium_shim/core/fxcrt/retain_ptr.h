#pragma once
#include <atomic>
#include <cstddef>
#include <utility>

class Retainable {
    mutable std::atomic<uint32_t> ref_count_{0};
public:
    virtual ~Retainable() = default;
    void Retain() const { ref_count_.fetch_add(1, std::memory_order_relaxed); }
    void Release() const {
        if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) delete this;
    }
    bool HasOneRef() const { return ref_count_.load(std::memory_order_acquire) == 1; }
};

template <typename T>
class RetainPtr {
    T* ptr_ = nullptr;
public:
    RetainPtr() = default;
    RetainPtr(std::nullptr_t) {}
    explicit RetainPtr(T* p) : ptr_(p) { if (ptr_) ptr_->Retain(); }
    RetainPtr(const RetainPtr& o) : ptr_(o.ptr_) { if (ptr_) ptr_->Retain(); }
    RetainPtr(RetainPtr&& o) noexcept : ptr_(std::exchange(o.ptr_, nullptr)) {}
    ~RetainPtr() { if (ptr_) ptr_->Release(); }

    RetainPtr& operator=(const RetainPtr& o) {
        if (this != &o) { if (o.ptr_) o.ptr_->Retain(); if (ptr_) ptr_->Release(); ptr_ = o.ptr_; }
        return *this;
    }
    RetainPtr& operator=(RetainPtr&& o) noexcept {
        if (this != &o) { if (ptr_) ptr_->Release(); ptr_ = std::exchange(o.ptr_, nullptr); }
        return *this;
    }
    void Reset(T* p = nullptr) { if (p) p->Retain(); if (ptr_) ptr_->Release(); ptr_ = p; }
    T* Get() const { return ptr_; }
    T* operator->() const { return ptr_; }
    T& operator*() const { return *ptr_; }
    explicit operator bool() const { return ptr_ != nullptr; }
    bool operator==(std::nullptr_t) const { return ptr_ == nullptr; }
    bool operator!=(std::nullptr_t) const { return ptr_ != nullptr; }
};

namespace pdfium {
template <typename T, typename... Args>
RetainPtr<T> MakeRetain(Args&&... args) { return RetainPtr<T>(new T(std::forward<Args>(args)...)); }
} // namespace pdfium