#ifndef __MESH_COMMAND_HPP__
#define __MESH_COMMAND_HPP__

#include <cstdint>
#include <string_view>
#include "esp_mesh.h"

#define MESH_MTU_SIZE          (1460)

#pragma pack(push, 1)

enum class message_type : uint32_t {
    KEEP_ALIVE = 0,
    START_KEEP_ALIVE,
    STOP_KEEP_ALIVE,
    BECOME_ROOT,
    GO_TO_SLEEP,
    GET_NODES,
    GET_NODES_REPLY,
    GET_STATISTICS,
    GET_STATISTICS_REPLY,
    FORWARD
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
        case message_type::BECOME_ROOT:
            return "become_root";
        case message_type::GO_TO_SLEEP:
            return "go_to_sleep";
        case message_type::GET_NODES:
            return "get_nodes";
        case message_type::GET_NODES_REPLY:
            return "get_nodes_reply";
        case message_type::GET_STATISTICS:
            return "get_statistics";
        case message_type::GET_STATISTICS_REPLY:
            return "get_statistics_reply";
        case message_type::FORWARD:
            return "forward";
    }
    return "invalid_type";
}

struct keep_alive_data {
    uint32_t message_index;
    uint64_t timestamp;
    int64_t rssi;
    uint8_t parent_mac[6];
    uint8_t layer;
    uint16_t payload_size;
    uint8_t payload[0];
};

struct start_keep_alive_data {
    my_bool reset_index;
    uint32_t delay_ms;
    my_bool send_to_root;
    uint16_t payload_size;
    uint8_t target_mac[6];
};

struct stop_keep_alive_data {

};

struct go_to_sleep_data {
    uint64_t sleep_time_ms;
};

struct get_nodes_reply_data {
    constexpr static std::size_t MAX_NODES = 15;

    uint8_t num_nodes;
    mesh_addr_t nodes[MAX_NODES];
};

struct statistics_node_info {
    uint64_t first_message_ms;
    uint64_t last_keep_alive_ms;
    uint64_t last_keep_alive_far_ms;
    uint64_t total_bytes_sent;
    uint64_t count_of_message;
    uint8_t mac[6];
    uint8_t parent_mac[6];
    uint8_t layer;
    uint64_t missed_messages;
    int64_t last_rssi;
};

struct statistics_tree_info_data {
    constexpr static std::size_t MAX_NODES = 15;

    uint8_t num_nodes;
    statistics_node_info nodes[MAX_NODES];
};

struct forward_data {
    uint8_t mac[6];
    uint8_t payload[0];
};

struct message_t {
    message_type type;
    uint16_t len = 0;
    union {
        keep_alive_data keep_alive;
        start_keep_alive_data start_keep_alive;
        stop_keep_alive_data stop_keep_alive;
        go_to_sleep_data go_to_sleep;
        get_nodes_reply_data get_nodes_reply;
        statistics_tree_info_data statistics_tree_info;
        forward_data forward;
    };
};

constexpr static inline size_t header_size = sizeof(message_type) + sizeof(uint16_t);

#pragma pack(pop)

int send_keep_alive(const mesh_addr_t *to, uint16_t extra_size);

int send_start_keep_alive(const mesh_addr_t *to, start_keep_alive_data start_keep_alive);
int send_stop_keep_alive(const mesh_addr_t *to);
int send_go_to_sleep(const mesh_addr_t *to, go_to_sleep_data go_to_sleep);

void keep_alive_task(void *arg);

void print_statistics();

int handle_message(const mesh_addr_t *from, const uint8_t *buff, size_t size);

#endif // __MESH_COMMAND_HPP__