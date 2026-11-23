#pragma once
#include <cstddef>
#include <type_traits>

template <typename T>
class UnownedPtr {
    T* ptr_ = nullptr;
public:
    constexpr UnownedPtr() = default;
    constexpr UnownedPtr(std::nullptr_t) noexcept : ptr_(nullptr) {}
    constexpr UnownedPtr(T* p) noexcept : ptr_(p) {}

    template <typename U>
        requires std::is_convertible_v<U*, T*>
    constexpr UnownedPtr(const UnownedPtr<U>& o) noexcept : ptr_(o.get()) {}

    constexpr UnownedPtr& operator=(T* p) noexcept { ptr_ = p; return *this; }
    constexpr UnownedPtr& operator=(std::nullptr_t) noexcept { ptr_ = nullptr; return *this; }

    constexpr T* get() const noexcept { return ptr_; }
    constexpr T* operator->() const noexcept { return ptr_; }
    constexpr T& operator*() const noexcept { return *ptr_; }
    constexpr operator T*() const noexcept { return ptr_; }
    constexpr explicit operator bool() const noexcept { return ptr_ != nullptr; }
    constexpr void Reset(T* p = nullptr) noexcept { ptr_ = p; }
};