#ifndef __MUTEX_HPP__
#define __MUTEX_HPP__

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"

class mutex {
public:
    mutex() {
        _l = xSemaphoreCreateBinaryStatic(&_sem);
        give();
    }

    mutex(const mutex &) = delete;
    mutex& operator=(const mutex &) = delete;
    mutex(mutex &&) = default;
    mutex& operator=(mutex &&) = default;

    bool take(uint64_t delay_ms=10000) {
        if (pdTRUE != xSemaphoreTake(_l, delay_ms / portTICK_PERIOD_MS)) {
            // ESP_LOGI(TAG, "Failed take");
            return false;
        }
        return true;
    }

    void give() {
        xSemaphoreGive(_l);
    }

private:
    SemaphoreHandle_t _l;
    StaticSemaphore_t _sem;
};

class unique_lock {
    public:
        explicit unique_lock(mutex &l) : _l(l) {
            _taken = _l.take();
        }

        unique_lock(const unique_lock &) = delete;
        unique_lock& operator=(const unique_lock &) = delete;
        unique_lock(unique_lock &&) = default;
        unique_lock& operator=(unique_lock &&) = default;

        ~unique_lock() {
            if (_taken)
                _l.give();
        }
    private:
        bool _taken;
        mutex &_l;
};

#endif /* __MUTEX_HPP__ */
