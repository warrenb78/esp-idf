#include <memory>
#include <list>
#include <array>
#include <cstdint>

class Pool {
public:
    static Pool &get_pool() {
        static Pool pool{10};
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
    using item_type = std::array<uint8_t, 255>;
    explicit Pool(size_t count) {
        for (size_t i = 0; i < count; ++i)
            _pool.push_back(std::make_unique<item_type>());
    }

    template <typename T>
    T *_take() {
        // static_assert(sizeof(T) < item_type::size());
        if (_pool.empty())
            return nullptr;
        auto ref = std::move(_pool.front());
        _pool.pop_front();
        return reinterpret_cast<T *>(ref.release());
    }

    template <typename T>
    void _give(T *ptr) {
        _pool.emplace_back(reinterpret_cast<item_type *>(ptr));
    }

    std::list<std::unique_ptr<item_type>> _pool;
};

#include <cstdio>

int main() {
    Pool &pool = Pool::get_pool();
    Pool::uptr<int> a = pool.take<int>();

    printf("%zu\n", sizeof(a));
}