#pragma once

#include "esp_heap_caps.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace Espressif::Wrappers::Audio::InternalRam {

// Warning: every object holding atomics that the mixer uses must be allocated here. With
// PSRAM, atomic read-modify-writes on external RAM fall back to a spinlock-protected libcall.
inline constexpr uint32_t kCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;

struct Deleter {
    template <typename T>
    void operator()(T* object) const {
        std::destroy_at(object);
        heap_caps_free(object);
    }
};

template <typename T>
class ArrayDeleter {
public:
    ArrayDeleter() = default;
    explicit ArrayDeleter(size_t count) : _count(count) {}

    void operator()(T* objects) const {
        std::destroy_n(objects, _count);
        heap_caps_free(objects);
    }

private:
    size_t _count = 0;
};

template <typename T>
using Ptr = std::unique_ptr<T, Deleter>;

template <typename T>
using Array = std::unique_ptr<T[], ArrayDeleter<T>>;

namespace detail {

struct StorageFree {
    void operator()(void* storage) const { heap_caps_free(storage); }
};

inline std::unique_ptr<void, StorageFree> allocate(size_t alignment, size_t bytes) {
    return std::unique_ptr<void, StorageFree>(heap_caps_aligned_alloc(alignment, bytes, kCaps));
}

} // namespace detail

template <typename T, typename D = Deleter, typename... Args>
std::unique_ptr<T, D> make(Args&&... args) {
    auto storage = detail::allocate(alignof(T), sizeof(T));
    if (!storage) return nullptr;
    T* object = std::construct_at(static_cast<T*>(storage.get()), std::forward<Args>(args)...);
    static_cast<void>(storage.release());
    return std::unique_ptr<T, D>(object);
}

template <typename T>
Array<T> makeArray(size_t count) {
    if (count == 0) return nullptr;
    auto storage = detail::allocate(alignof(T), sizeof(T) * count);
    if (!storage) return nullptr;
    T* objects = static_cast<T*>(storage.get());
    std::uninitialized_default_construct_n(objects, count);
    static_cast<void>(storage.release());
    return Array<T>(objects, ArrayDeleter<T>(count));
}

} // namespace Espressif::Wrappers::Audio::InternalRam
