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
// How many messages to keep for special commands.
constexpr static size_t DEFAULT_THRESHOLD = 5;
constexpr static size_t IMPORTANT_THRESHOLD = 0;

// Our structs have a constructor that zeros out them.
template <typename T>
constexpr inline bool is_pod_v = std::is_standard_layout_v<T>;// && std::is_trivial_v<T>;

template <size_t PoolSize>
class Pool {
public:
    static Pool<PoolSize> &get_pool() {
        // C++11 singleton pattern - will be initialized on the first creation
        static Pool<PoolSize> pool{};
        return pool;
    }

    // Unique_ptr custom deleter, stateless to leverage empty class optimization,
    // i.e that unique_ptr still only takes the space of single pointer.
    template <typename T>
    struct AutoGiver {
        void operator()(T *ptr) const noexcept {
            if (!ptr)
                return;
            get_pool()._give(ptr);
        }
    };

    template <typename T>
    using uptr = std::unique_ptr<T, AutoGiver<T>>;

    // @brief Takes an item from the pool, it will be returned to pool
    //     automatically when item lifetime ends (RAII)
    //
    // @param is_important - True if message needs priority and can use
    //     all message, by default restricted to use parts of the pool.
    template <typename T>
    uptr<T> take(bool is_important)
    {
        const size_t threshold = is_important ? IMPORTANT_THRESHOLD : DEFAULT_THRESHOLD;
        return uptr<T>{_take<T>(threshold)};
    }
    
private:
    // Dummy bytes array. Assumes default alignment is good enough
    // for `T` types used with this class otherwise bad things can happen
    // (TODO: validate T alignment requirement is not too much.)
    using item_type = std::array<uint8_t, MAX_MESSAGE_SIZE>;
    explicit Pool() {
        for (auto &item : _pool)
            item = std::make_unique<item_type>();
    }

    // @param threshold - take only if pool has more than threshold
    //     items to take. Otherwise return an empty item (nullptr)
    template <typename T, class = std::enable_if_t<is_pod_v<T>>>
    T *_take(size_t threshold) {
        static_assert(sizeof(T) < item_type().size(), "Tried to take too large message");
        // Lock for multi task usage of pool.
        unique_lock guard(_mutex);

        if (_size <= threshold) // empty pool.
            return nullptr;
        // Take ownership over the item.
        auto ref = std::move(_pool[_tail]);
        if (ref == nullptr)
            // Protect against programming error - an item in the "available" range
            // is empty and can not be really taken.
            abort();
        // Book keeping about item being taken.
        _tail = _next_index(_tail);
        --_size;
        return reinterpret_cast<T *>(ref.release());
    }

    // Shouldn't be called directly, but only via unique_ptr returned
    // from the take function.
    // Can be bypassed and not used correctly - by assumes no miss use.
    template <typename T>
    void _give(T *ptr) {
        unique_lock guard(_mutex);
        if (_size >= PoolSize || _pool[_head] != nullptr)
            // Protect against programming error - too many items returned to pool
            // or possible booking keeping error trying to return into already populated
            // item.
            abort();
        // Pool takes ownership back on ptr.
        _pool[_head].reset(reinterpret_cast<item_type *>(ptr));
        _head = _next_index(_head);
        ++_size;
    }

    size_t _next_index(size_t index) {
        return (index + 1) % PoolSize;
    }

    // Real owner of the items (free back to os on pool destruction).
    std::array<std::unique_ptr<item_type>, PoolSize> _pool;

    // Book keeping variables (cyclic buffer pattern).
    size_t _size = PoolSize;
    size_t _head = 0;
    size_t _tail = 0;
    // Lock for multi task support.
    mutex _mutex;
};

using DefPool = Pool<MAX_NUMBER_OF_MESSAGE>;

#endif /* __MEMORY_POOL__ */
