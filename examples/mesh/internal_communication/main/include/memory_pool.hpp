#ifndef __MEMORY_POOL_HPP__
#define __MEMORY_POOL_HPP__

#include <type_traits>
#include <memory>
#include <list>
#include <array>
#include <cstdint>

#include "mutex.hpp"

// 1 mbit/s / 255 KB per message ~ 500 message / s
constexpr static size_t MAX_NUMBER_OF_MESSAGE = 500;
constexpr static size_t MAX_MESSAGE_SIZE = 261; // 255 for transmit + 6 for metadata

// Our structs have a constructor that zeros out them.
template <typename T>
constexpr inline bool is_pod_v = std::is_standard_layout_v<T>;// && std::is_trivial_v<T>;

template <size_t PoolSize>
class Pool {
public:
    static Pool<PoolSize> &get_pool() {
        static Pool<PoolSize> pool{};
        return pool;
    }

    template <typename T>
    struct AutoGiver {
        void operator()(T *ptr) const {
            if (!ptr)
                return;
            get_pool()._give(ptr);
        }
    };

    template <typename T>
    using uptr = std::unique_ptr<T, AutoGiver<T>>;

    template <typename T>
    uptr<T> take()
    {
        return uptr<T>{_take<T>()};
    }
    
private:
    using item_type = std::array<uint8_t, MAX_MESSAGE_SIZE>;
    explicit Pool() {
        for (auto &item : _pool)
            item = std::make_unique<item_type>();
    }

    template <typename T, class = std::enable_if_t<is_pod_v<T>>>
    T *_take() {
        static_assert(sizeof(T) < item_type().size(), "Tried to take too large message");
        if (_size == 0) // empty pool.
            return nullptr;
        unique_lock guard(_mutex);
        auto ref = std::move(_pool[_tail]);
        if (ref == nullptr)
            abort(); // Programming error cond
        _tail = _next_index(_tail);
        --_size;
        return reinterpret_cast<T *>(ref.release());
    }

    template <typename T>
    void _give(T *ptr) {
        if (_size >= PoolSize || _pool[_head] != nullptr)
            abort();
        unique_lock guard(_mutex);
        _pool[_head].reset(reinterpret_cast<item_type *>(ptr));
        _head = _next_index(_head);
        ++_size;
    }

    size_t _next_index(size_t index) {
        return (index + 1) % PoolSize;
    }

    std::array<std::unique_ptr<item_type>, PoolSize> _pool;
    size_t _size = PoolSize;
    size_t _head = 0;
    size_t _tail = 0;
    mutex _mutex;
};

using DefPool = Pool<MAX_NUMBER_OF_MESSAGE>;

#endif /* __MEMORY_POOL__ */
