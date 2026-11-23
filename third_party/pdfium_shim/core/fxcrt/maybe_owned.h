#pragma once
#include <memory>
#include <utility>

template <typename T>
class MaybeOwned {
    std::unique_ptr<T> owned_;
    T* ptr_ = nullptr;
public:
    MaybeOwned() = default;
    MaybeOwned(std::nullptr_t) {}
    MaybeOwned(T* p) : ptr_(p) {}
    MaybeOwned(std::unique_ptr<T> p) : owned_(std::move(p)), ptr_(owned_.get()) {}
    MaybeOwned(MaybeOwned&& o) noexcept : owned_(std::move(o.owned_)), ptr_(std::exchange(o.ptr_, nullptr)) {}
    MaybeOwned& operator=(MaybeOwned&& o) noexcept {
        owned_ = std::move(o.owned_); ptr_ = std::exchange(o.ptr_, nullptr); return *this;
    }
    MaybeOwned& operator=(T* p) { owned_.reset(); ptr_ = p; return *this; }
    MaybeOwned& operator=(std::unique_ptr<T> p) { owned_ = std::move(p); ptr_ = owned_.get(); return *this; }
    void Reset(T* p = nullptr) { owned_.reset(); ptr_ = p; }
    void Reset(std::unique_ptr<T> p) { owned_ = std::move(p); ptr_ = owned_.get(); }
    bool IsOwned() const { return owned_ != nullptr; }
    T* Get() const { return ptr_; }
    T* operator->() const { return ptr_; }
    T& operator*() const { return *ptr_; }
    explicit operator bool() const { return ptr_ != nullptr; }
};