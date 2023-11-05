#ifndef __MESH_COMMAND_HPP__
#define __MESH_COMMAND_HPP__

#include <cstdint>
#include <string_view>

#define MESH_MTU_SIZE          (1460)

#pragma pack(push, 1)

enum class message_type : uint32_t {
    KEEP_ALIVE = 0,
};

inline constexpr const char * type_to_name(message_type type) {
    switch (type) {
        case message_type::KEEP_ALIVE:
            return "keep_alive";
    }
    return "invalid_type";
}


struct keep_alive_data {
    uint32_t message_index;
};

struct message_t {
    message_type type;
    union {
        keep_alive_data keep_alive;
    };
};

#pragma pack(pop)

int send_keep_alive(const mesh_addr_t *to);

int handle_message(const mesh_addr_t *from, const uint8_t *buff, size_t size);

#endif // __MESH_COMMAND_HPP__