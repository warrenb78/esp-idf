#include "esp_log.h"
#include "esp_mac.h"
#include "esp_mesh.h"
#include "esp_mesh_internal.h"

#include "command.hpp"

static const char *COMMAND_TAG = "command";


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

int send_keep_alive(const mesh_addr_t *to)
{
    message_t message = {
        .type = message_type::KEEP_ALIVE,
        .keep_alive = {
            .message_index = get_message_count(false),
        },
    };
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
        ESP_LOGE(COMMAND_TAG, "Got error %d when trying to send to root", err);
        return err;
    }
    return ESP_OK;
}

int handle_message(const mesh_addr_t *from, const uint8_t *buff, size_t size)
{
    const auto *message = reinterpret_cast<const message_t *>(buff);
    const char *message_name = type_to_name(message->type);
    switch (message->type) {
        case message_type::KEEP_ALIVE:
            ESP_LOGI(COMMAND_TAG, "message %s from:" MACSTR ", id %lu",
                     message_name, MAC2STR(from->addr),
                     message->keep_alive.message_index);
            break;
    }
    return ESP_OK;
}
