#include <atomic>
#include <map>
#include <unordered_map>

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_mesh.h"
#include "esp_mesh_internal.h"
#include "driver/gptimer.h"

#include "command.hpp"
#include "general.hpp"
#include "uart_commander.hpp"

static const char *TAG = "command";
#if defined(USE_ZHNETWORK)
const start_keep_alive_data BASIC_KEEP_ALIVE {
    .reset_index = my_bool::TRUE,
    .delay_ms = 500, // 2 Hz,
    .send_to_root = my_bool::FALSE,
    .payload_size = 0,
    .target_mac{0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
};
#else
const start_keep_alive_data BASIC_KEEP_ALIVE {
    .reset_index = my_bool::TRUE,
    .delay_ms = 500, // 2 Hz,
    .send_to_root = my_bool::TRUE,
    .payload_size = 0,
    .target_mac{},
};
#endif

uint32_t get_message_count(bool reset);

class keep_aliver {
public:
    void stop() {
        ESP_LOGE(TAG, "got stop keep alive");
        _do_work = false;
    }

    void start(start_keep_alive_data start_data) {
        _do_work = true;
        _delay_ms = start_data.delay_ms;
        _delay_ticks = _delay_ms / portTICK_PERIOD_MS;
        ESP_LOGW(TAG, "Got start keep alive delay ms %lu, delay ticks %lu",
                 _delay_ms, _delay_ticks);
        _send_to_root = to_bool(start_data.send_to_root);
        memcpy(_target.addr, start_data.target_mac, sizeof(start_data.target_mac));
        if (start_data.payload_size > keep_alive_size)
            _extra_size = start_data.payload_size - keep_alive_size;
        else
            _extra_size = 0;
        if (to_bool(start_data.reset_index))
            get_message_count(true);
        _last_wake_time = xTaskGetTickCount();
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
                send_keep_alive(target, _extra_size);
                if (_delay_ticks > 0)
                    xTaskDelayUntil(&_last_wake_time, _delay_ticks);
                continue;
            } else {
                vTaskDelay(10 / portTICK_PERIOD_MS);
            }
        }
    }
private:
    TickType_t _last_wake_time;
    std::atomic<bool> _do_work = false;
    uint32_t _delay_ms;
    TickType_t _delay_ticks;
    bool _send_to_root;
    mesh_addr_t _target;
    uint16_t _extra_size;
};

// Make mesh_addr_t hashable.
template <>
struct std::hash<mesh_addr_t> {
    std::size_t operator()(const mesh_addr_t &addr) const noexcept {
        std::size_t value = std::size(addr.addr);
        for (auto byte : addr.addr) {
            // basic hash combine - should sanely distribute bits.
            value = std::hash<uint8_t>{}(byte) + (value << 6) + (value >> 2);
        }
        return value;
    }
};

bool operator==(const mesh_addr_t &lhs, const mesh_addr_t &rhs)
{
    return 0 == memcmp(&lhs, &rhs, sizeof(lhs));
}

bool operator<(const mesh_addr_t &lhs, const mesh_addr_t &rhs)
{
    return 0 > memcmp(&lhs, &rhs, sizeof(lhs));
}

class statistics {
public:
    struct metrics {
        uint64_t first_message_ms;
        uint64_t last_keep_alive_timestamp_ms;
        uint64_t last_keep_alive_far_timestamp_ms;
        uint64_t total_bytes_sent;
        uint64_t count_of_commands;
        uint8_t parent[6];
        uint8_t layer;
        uint32_t last_message_id;
        uint64_t missed_messages;
        int64_t last_rssi;
    };

    void clear(mesh_addr_t addr) {
        _info.erase(addr);
    }

    void clear() {
        _info.clear();
    }

    // Will set the addr if not existing.
    metrics &get_node_info(mesh_addr_t addr) {
        return _info[addr];
    }

    const metrics &get_node_info(mesh_addr_t addr) const noexcept {
        return _info.at(addr);
    }

    // return singleton for info.
    static statistics &get_state() {
        // C++11 singeltons.
        static statistics state;
        return state;
    }

    auto begin() {
        return _info.begin();
    }

    auto begin() const {
        return _info.begin();
    }

    auto end() {
        return _info.end();
    }

    auto end() const {
        return _info.end();
    }

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
                ESP_LOGI(TAG, "Failed take");
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

    unique_lock lock() const{
        return unique_lock{_lock};
    }

private:
#if 1
    template<typename K, typename V>
    using map_t = std::map<K, V>;
#else
    template<typename K, typename V>
    using map_t = std::unordered_map<K, V>;
#endif
    mutable mutex _lock;
    map_t<mesh_addr_t, metrics> _info;
};

static keep_aliver g_keep_alive;
static ISender *g_sender;

void set_sender(ISender *sender) {
    g_sender = sender;
}

void keep_alive_task(void *arg) {
    (void)arg;
    g_keep_alive.start(BASIC_KEEP_ALIVE);
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

int send_message(const mesh_addr_t *to, message_t &message, size_t size = sizeof(message_t)){
    message.len = size - header_size;
    if (!g_sender) {
        ESP_LOGE(TAG, "Tried to send while sender not set");
        return ESP_FAIL;
    }

    return g_sender->send(reinterpret_cast<uint8_t *>(&message), size, to->addr);
}

static __attribute__((aligned(16))) uint8_t tx_buf[MESH_MTU_SIZE] = { 0, };

int send_keep_alive(const mesh_addr_t *to, uint16_t extra_size)
{
    int rssi = 0;
    mesh_addr_t parent{};
    uint8_t layer{};
#ifndef USE_ZHNETWORK
    int err = esp_wifi_sta_get_rssi(&rssi);
    if (err) {
        ESP_LOGE(TAG, "Failed to get rssi %d", err);
        return err;
    }

    err = esp_mesh_get_parent_bssid(&parent);
    if (err)
        return err;

    layer = static_cast<uint8_t>(esp_mesh_get_layer());
#endif

    message_t &message = *reinterpret_cast<message_t *>(tx_buf);
    message = {
        .type = message_type::KEEP_ALIVE,
        .keep_alive = {
            .message_index = get_message_count(false),
            .timestamp = esp_timer_get_time() / 1000ull,
            .rssi = rssi, 
            .parent_mac = {},
            .layer = layer,
            .payload_size = extra_size,
            .payload = {}
        },
    };
    memcpy(message.keep_alive.parent_mac, parent.addr, sizeof(parent.addr));
    return send_message(to, message, extra_size + keep_alive_size);
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

void print_statistics() {
    const statistics &state = statistics::get_state();
    auto guard = state.lock();
    ESP_LOGI(TAG,
        "mac\t\t  |parent\t\t |layer\t |count\t"
        " |last rssi\t |last time[ms]\t|last far time[ms]\t |missed\t|KB/s");
    for (const auto &[addr, info] : state) {

        double kbps = [&]() -> double{
            if (info.last_keep_alive_timestamp_ms == info.first_message_ms) {
                return 0;
            }
            return info.total_bytes_sent / ((info.last_keep_alive_timestamp_ms - info.first_message_ms) / 1000.);
        }();
        ESP_LOGI(TAG, MACSTR "  " MACSTR " %u %04llu   %03lld \t %06llu \t %06llu \t %04llu \t %g",
                 MAC2STR(addr.addr), MAC2STR(info.parent), info.layer,
                 info.count_of_commands, info.last_rssi, info.last_keep_alive_timestamp_ms,
                 info.last_keep_alive_far_timestamp_ms,
                 info.missed_messages, kbps);
    }
}

void handle_keep_alive(const mesh_addr_t *from, const message_t *message, size_t size)
{
    statistics &state = statistics::get_state();
    auto guard = state.lock();
    statistics::metrics &metrics = state.get_node_info(*from);
    const auto &keep_alive = message->keep_alive;
    if (keep_alive.message_index == 0) {
        metrics = {}; // To zero out.
    }
    uint64_t current_time_ms = esp_timer_get_time() / 1000ull;
    if (metrics.count_of_commands == 0) {
        // first time
        metrics.first_message_ms = current_time_ms;
    }
    ++metrics.count_of_commands;
    metrics.total_bytes_sent += size;

    metrics.last_keep_alive_timestamp_ms = current_time_ms;
    metrics.last_keep_alive_far_timestamp_ms = keep_alive.timestamp;
    metrics.last_rssi = keep_alive.rssi;
    metrics.layer = keep_alive.layer;
    memcpy(metrics.parent, keep_alive.parent_mac, sizeof(metrics.parent));
    if (keep_alive.message_index > metrics.last_message_id) {
        metrics.missed_messages += (keep_alive.message_index - 1 - metrics.last_message_id);
    }
    metrics.last_message_id = keep_alive.message_index;

#if 0
    const char *message_name = "keep_alive";
    ESP_LOGI(TAG,
            "message %s from:" MACSTR "[%u] parent: " MACSTR " , id %lu timestamp %llu ms, rssi %lld",
            message_name, MAC2STR(from->addr), keep_alive.layer, MAC2STR(keep_alive.parent_mac),
            keep_alive.message_index, keep_alive.timestamp, keep_alive.rssi);
#endif
}

void handle_get_nodes()
{
    message_t &nodes_reply = *reinterpret_cast<message_t *>(tx_buf);
    nodes_reply = {
        .type = message_type::GET_NODES_REPLY,
        .len = sizeof(get_nodes_reply_data),
        .get_nodes_reply{},
    };
    auto &get_nodes_reply = nodes_reply.get_nodes_reply;
    int num_nodes = 0;
    int err = esp_mesh_get_routing_table(
        get_nodes_reply.nodes, sizeof(get_nodes_reply.nodes), &num_nodes);
    if (err) {
        ESP_LOGE(TAG, "Failed get routing table %d", err);
        get_nodes_reply.num_nodes = 0;
    } else {
        get_nodes_reply.num_nodes = static_cast<uint8_t>(num_nodes);
    }

    send_uart_bytes((uint8_t *)&nodes_reply, sizeof(get_nodes_reply) + header_size);
}

void handle_get_statistics()
{
    message_t &reply = *reinterpret_cast<message_t *>(tx_buf);
    reply = {
        .type = message_type::GET_STATISTICS_REPLY,
        .len = sizeof(statistics_tree_info_data),
        .statistics_tree_info{},
    };
    auto &tree_info = reply.statistics_tree_info;
    tree_info.current_ms = esp_timer_get_time() / 1000ull;
    statistics &state = statistics::get_state();
    {
        auto guard = state.lock();
        uint32_t i = 0;
        for (const auto &[mac, src_info] : state) {
            auto &node_info = tree_info.nodes[i];
            memcpy(node_info.mac, mac.addr, sizeof(node_info.mac));
            memcpy(node_info.parent_mac, src_info.parent, sizeof(node_info.parent_mac));
            node_info.first_message_ms = src_info.first_message_ms;
            node_info.last_keep_alive_ms = src_info.last_keep_alive_timestamp_ms;
            node_info.last_keep_alive_far_ms = src_info.last_keep_alive_far_timestamp_ms;
            node_info.total_bytes_sent = src_info.total_bytes_sent;
            node_info.count_of_message = src_info.count_of_commands;
            node_info.layer = src_info.layer;
            node_info.missed_messages = src_info.missed_messages;
            node_info.last_rssi = src_info.last_rssi;
            ++i;
        }
        tree_info.num_nodes = i;
    }
    send_uart_bytes((uint8_t *)&reply, sizeof(tree_info) + header_size);
}

static bool restart_esp(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_data)
{
    esp_restart();
}

int handle_message(const mesh_addr_t *from, const uint8_t *buff, size_t size)
{
    const auto *message = reinterpret_cast<const message_t *>(buff);
    const char *message_name = type_to_name(message->type);

    switch (message->type) {
        case message_type::KEEP_ALIVE: {
            if (from) {
                handle_keep_alive(from, message, size);
            } else {
                ESP_LOGI(TAG, "keep alive without from");
            }
            break;
        }
        case message_type::START_KEEP_ALIVE: 
            g_keep_alive.start(message->start_keep_alive);
            break;
        case message_type::STOP_KEEP_ALIVE:
            g_keep_alive.stop();
            g_keep_alive.start(BASIC_KEEP_ALIVE);
            break;
        case message_type::GO_TO_SLEEP: {
            ESP_LOGW(TAG, "message %s from: " MACSTR " going to sleep for %llu ms",
                     message_name, MAC2STR(from->addr), message->go_to_sleep.sleep_time_ms);
            g_keep_alive.stop();
            esp_mesh_stop();
            vTaskDelay(message->go_to_sleep.sleep_time_ms / portTICK_PERIOD_MS);
            ESP_LOGW(TAG, "message %s from: " MACSTR " waking up",
                     message_name, MAC2STR(from->addr));
            app_mesh_start();
            g_keep_alive.start(BASIC_KEEP_ALIVE);
            break;
        }
        case message_type::BECOME_ROOT:
            g_keep_alive.stop();
            vTaskDelay(10 / portTICK_PERIOD_MS);
            esp_mesh_set_type(MESH_ROOT);
            break;
        case message_type::GET_NODES:
            handle_get_nodes();
            break;
        case message_type::GET_STATISTICS:
            handle_get_statistics();
            break;
        case message_type::CLEAR_STATISTICS: {
            statistics &state = statistics::get_state();
            auto guard = state.lock();
            state.clear();
            break;
        }
        case message_type::GET_NODES_REPLY:
        case message_type::GET_STATISTICS_REPLY:
        case message_type::GET_LATENCY_REPLY:
            ESP_LOGW(TAG, "got non relevant message %s", message_name);
            break;
        case message_type::FORWARD: {
            if (message->forward.to_host) {
                // TODO: 7 is mac + to_host.
                ESP_LOGI(TAG, "Forwarding to host bytes %u", message->len - fwd_size);
                send_uart_bytes(message->forward.payload, message->len - fwd_size);
            } else {
                mesh_addr_t dest;
                memcpy(&dest, message->forward.mac, sizeof(dest));
                ESP_LOGI(TAG, "Forwarding to %.2x:%.2x:%.2x:%.2x:%.2x:%.2x\n", 
                    dest.addr[0], dest.addr[1], dest.addr[2], dest.addr[3], dest.addr[4], dest.addr[5]);
                send_message(&dest, *(message_t*)&message->forward.payload, message->len - fwd_size);
            }
            break;
        }
        case message_type::ECHO_REQUEST: {
            ESP_LOGI(TAG, "Got echo requets %llu", message->echo_data.timestamp);
            message_t &msg = *reinterpret_cast<message_t *>(tx_buf);
            memcpy(&msg, message, size);
            msg.type = message_type::ECHO_REPLY;
            send_message(from, msg);
            break;
        }
        case message_type::ECHO_REPLY: {
            message_t &reply = *reinterpret_cast<message_t *>(tx_buf);
            reply = {
               .type = message_type::FORWARD,
               .forward = {
                   .mac{},
                   .to_host = 1,
                   .payload{},
               }
            };
            message_t &inner = *reinterpret_cast<message_t *>(reply.forward.payload);
            inner = {
                .type = message_type::GET_LATENCY_REPLY,
                .len = sizeof(get_latency_reply_data),
                .get_latency_reply = {
                    .start_ms = message->echo_data.timestamp,
                    .end_ms = esp_timer_get_time() / 1000ull,
                },
            };
            ESP_LOGI(TAG, "got echo reply %llu --> %llu",
                     inner.get_latency_reply.start_ms,
                     inner.get_latency_reply.end_ms);
            send_message(nullptr, reply, header_size * 2 + sizeof(forward_data) + sizeof(get_latency_reply_data));
            break;
        }
        case message_type::GET_LATENCY: {
            message_t &msg = *reinterpret_cast<message_t *>(tx_buf);
            msg = {
                .type = message_type::ECHO_REQUEST,
                .len = sizeof(echo_data_t),
                .echo_data = {
                    .timestamp = esp_timer_get_time() / 1000ull,
                },
            };
            ESP_LOGI(TAG, "send get echo to " MACSTR, MAC2STR(message->get_latency.dst));
            mesh_addr_t target{};
            memcpy(target.addr, message->get_latency.dst, sizeof(target.addr));
            send_message(&target, msg);
            break;
        }
        case message_type::SET_TOPOLOGY: {
            ESP_ERROR_CHECK(nvs_set_i32(mesh_nvs_handle, "topology", message->set_topology.topology));
            ESP_ERROR_CHECK(nvs_commit(mesh_nvs_handle));
            ESP_LOGI(TAG, "updating topology in next run to %ld", message->set_topology.topology);
            break;
        }
        case message_type::RESTART: {
            gptimer_handle_t gptimer = NULL;
            gptimer_config_t timer_config = {
                .clk_src = GPTIMER_CLK_SRC_DEFAULT,
                .direction = GPTIMER_COUNT_UP,
                .resolution_hz = 1000000, // 1MHz, 1 tick=1us
            };
            gptimer_event_callbacks_t cbs = {
                .on_alarm = restart_esp,
            };
            ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));
            ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, NULL));
            ESP_ERROR_CHECK(gptimer_enable(gptimer));

            gptimer_alarm_config_t alarm_config1 = {
                .alarm_count = 5000000, // period = 5s
            };
            ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config1));
            ESP_ERROR_CHECK(gptimer_start(gptimer));
            ESP_LOGE(TAG, "RESTARTING IN 5 SECONDS!");
            break;
        }
        case message_type::SET_LONG_RANGE:
            ESP_ERROR_CHECK(nvs_set_i32(mesh_nvs_handle, "long_range", message->set_long_range.long_range));
            ESP_ERROR_CHECK(nvs_commit(mesh_nvs_handle));
            ESP_LOGI(TAG, "updating long_range in next run to %ld", message->set_long_range.long_range);
            break;
    }
    return ESP_OK;
}
