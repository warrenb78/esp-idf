#include <atomic>

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_mesh.h"
#include "esp_mesh_internal.h"

#include "command.hpp"
#include "general.hpp"

static const char *COMMAND_TAG = "command";

uint32_t get_message_count(bool reset);

class keep_aliver {
public:
    void stop() {
        ESP_LOGE(COMMAND_TAG, "got stop keep alive");
        _do_work = false;
    }

    void start(start_keep_alive_data start_data) {
        ESP_LOGW(COMMAND_TAG, "got start keep alive");
        _do_work = true;
        _delay_ms = start_data.delay_ms;
        _send_to_root = to_bool(start_data.send_to_root);
        memcpy(_target.addr, start_data.target_mac, sizeof(start_data.target_mac));
        if (to_bool(start_data.reset_index))
            get_message_count(true);
    }

    void main_loop() {
        while (true) {
            if (_do_work) {
                const auto *target = [&]() -> const mesh_addr_t *{
                    if (_send_to_root) {
                        return nullptr;   
                    }
                    return &_target;
                }();
                send_keep_alive(target);
                vTaskDelay(_delay_ms / portTICK_PERIOD_MS);
                continue;
            } else {
                vTaskDelay(100 / portTICK_PERIOD_MS);
            }
        }
    }
private:
    std::atomic<bool> _do_work = false;
    uint32_t _delay_ms;
    bool _send_to_root;
    mesh_addr_t _target;
};

keep_aliver g_keep_alive;

void keep_alive_task(void *arg) {
    (void)arg;
    g_keep_alive.main_loop();
}

uint32_t get_message_count(bool reset)
{
    // Count of messages sent
    static uint32_t message_count = 0;

    if (reset) {
        message_count = 0;
        return message_count;
    }

    uint32_t res = message_count;
    ++message_count;
    return res;
}

int send_message(const mesh_addr_t *to, message_t &message){
    mesh_data_t data;
    data.data = (uint8_t *)&message;
    data.size = sizeof(message);
    if (data.size > MESH_MTU_SIZE) {
        ESP_LOGE(COMMAND_TAG, "message to large %u limit %u", data.size, MESH_MTU_SIZE);
        return ESP_FAIL;
    }
    data.proto = MESH_PROTO_BIN;
    data.tos = MESH_TOS_P2P;

    int err = esp_mesh_send(to, &data, MESH_DATA_P2P, nullptr, 0);
    if (err != ESP_OK) {
        if (to) {
            ESP_LOGE(COMMAND_TAG, "Got error %d when trying to send to " MACSTR, err, MAC2STR(to->addr));
        } else {
            ESP_LOGE(COMMAND_TAG, "Got error %d when trying to send to root", err);
        }
        return err;
    }
    return ESP_OK;
}

int send_keep_alive(const mesh_addr_t *to)
{
    int rssi = 0;
    int err = esp_wifi_sta_get_rssi(&rssi);
    if (err)
        return err;
    message_t message = {
        .type = message_type::KEEP_ALIVE,
        .keep_alive = {
            .message_index = get_message_count(false),
            .timestamp = esp_timer_get_time() / 1000ull,
            .rssi = rssi, 
        },
    };
    return send_message(to, message);
}

int send_start_keep_alive(const mesh_addr_t *to, start_keep_alive_data start_keep_alive)
{
    message_t message = {
        .type = message_type::START_KEEP_ALIVE,
        .start_keep_alive = start_keep_alive,
    };
    return send_message(to, message);
}

int send_stop_keep_alive(const mesh_addr_t *to)
{
    message_t message = {
        .type = message_type::STOP_KEEP_ALIVE,
        .stop_keep_alive = {}
    };
    return send_message(to, message);
}

int send_go_to_sleep(const mesh_addr_t *to, go_to_sleep_data go_to_sleep)
{
    message_t message = {
        .type = message_type::GO_TO_SLEEP,
        .go_to_sleep = go_to_sleep,
    };
    return send_message(to, message);
}

int handle_message(const mesh_addr_t *from, const uint8_t *buff, size_t size)
{
    const auto *message = reinterpret_cast<const message_t *>(buff);
    const char *message_name = type_to_name(message->type);
    // ESP_LOGI(COMMAND_TAG, "message %s from: " MACSTR, message_name, MAC2STR(from->addr));
    switch (message->type) {
        case message_type::KEEP_ALIVE:
            ESP_LOGI(COMMAND_TAG, "message %s from:" MACSTR ", id %lu timestamp %llu ms, rssi %lld",
                     message_name, MAC2STR(from->addr),
                     message->keep_alive.message_index, message->keep_alive.timestamp,
                     message->keep_alive.rssi);
            break;
        case message_type::START_KEEP_ALIVE: 
            g_keep_alive.start(message->start_keep_alive);
            break;
        case message_type::STOP_KEEP_ALIVE:
            g_keep_alive.stop();
            break;
        case message_type::GO_TO_SLEEP: {
            ESP_LOGW(COMMAND_TAG, "message %s from: " MACSTR " going to sleep for %llu ms",
                     message_name, MAC2STR(from->addr), message->go_to_sleep.sleep_time_ms);
            g_keep_alive.stop();
            esp_mesh_stop();
            vTaskDelay(message->go_to_sleep.sleep_time_ms / portTICK_PERIOD_MS);
            ESP_LOGW(COMMAND_TAG, "message %s from: " MACSTR " waking up",
                     message_name, MAC2STR(from->addr));
            app_mesh_start();
            break;
        }
    }
    return ESP_OK;
}
