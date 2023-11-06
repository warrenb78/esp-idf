#ifndef __MESH_COMMAND_HPP__
#define __MESH_COMMAND_HPP__

#include <cstdint>
#include <string_view>

#define MESH_MTU_SIZE          (1460)

#pragma pack(push, 1)

enum class message_type : uint32_t {
    KEEP_ALIVE = 0,
    START_KEEP_ALIVE,
    STOP_KEEP_ALIVE,
    
    GO_TO_SLEEP,
};

enum class my_bool : uint8_t {
    FALSE = 0,
    TRUE,
};

inline bool to_bool(my_bool b) {
    return static_cast<bool>(b);
}

inline my_bool from_bool(bool b) {
    if (b)
        return my_bool::TRUE;
    else
        return my_bool::FALSE;
}

inline constexpr const char * type_to_name(message_type type) {
    switch (type) {
        case message_type::KEEP_ALIVE:
            return "keep_alive";
        case message_type::START_KEEP_ALIVE:
            return "start_keep_alive";
        case message_type::STOP_KEEP_ALIVE:
            return "stop_keep_alive";
        case message_type::GO_TO_SLEEP:
            return "go_to_sleep";
    }
    return "invalid_type";
}


struct keep_alive_data {
    uint32_t message_index;
    uint64_t timestamp;
    int64_t rssi;
    uint8_t parent_mac[6];
    uint8_t layer;
};

struct start_keep_alive_data {
    my_bool reset_index;
    uint32_t delay_ms;
    my_bool send_to_root;
    uint8_t target_mac[6];
};

struct stop_keep_alive_data {

};

struct go_to_sleep_data {
    uint64_t sleep_time_ms;
};

struct message_t {
    message_type type;
    union {
        keep_alive_data keep_alive;
        start_keep_alive_data start_keep_alive;
        stop_keep_alive_data stop_keep_alive;
        go_to_sleep_data go_to_sleep;
    };
};

#pragma pack(pop)

int send_keep_alive(const mesh_addr_t *to);

int send_start_keep_alive(const mesh_addr_t *to, start_keep_alive_data start_keep_alive);
int send_stop_keep_alive(const mesh_addr_t *to);
int send_go_to_sleep(const mesh_addr_t *to, go_to_sleep_data go_to_sleep);

void keep_alive_task(void *arg);

void print_statistics();

int handle_message(const mesh_addr_t *from, const uint8_t *buff, size_t size);

#endif // __MESH_COMMAND_HPP__