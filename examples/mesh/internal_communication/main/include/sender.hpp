#ifndef __SENDER_HPP__
#define __SENDER_HPP__

#include <stdint.h>

#include <esp_mesh.h>
#include <esp_mac.h>
#include "esp_log.h"

#include "ZHNetwork.h"

#ifndef TAG
#define TAG "Sender"
#define SENDER_UNSET_TAG
#endif

class ISender {
public:
    virtual int send(uint8_t *msg, size_t msg_size, const uint8_t *mac) = 0;
    virtual size_t get_mtu_size() const = 0;
};

class MeshSender : public ISender {
public:
    size_t get_mtu_size() const override {
        return 1460;
    }

    int send(uint8_t *msg, size_t msg_size, const uint8_t *mac) override {
        mesh_data_t data;
        data.data = msg;
        data.size = msg_size;
        data.proto = MESH_PROTO_BIN;
        data.tos = MESH_TOS_P2P;
        int flag = [&]() -> int {
            if (mac == nullptr)
                return 0;
            return MESH_DATA_P2P;
        }();
        int err = esp_mesh_send(reinterpret_cast<const mesh_addr_t *>(mac), &data, flag, nullptr, 0);
        if (err != ESP_OK) {
            if (mac) {
                ESP_LOGE(TAG, "Got error %d when trying to send to " MACSTR, err, MAC2STR(mac));
            } else {
                ESP_LOGE(TAG, "Got error %d when trying to send to root", err);
            }
            return err;
        }
        return ESP_OK;
    }
};

class ZHSender : public ISender {
public:
    explicit ZHSender(ZHNetwork *network) : _network(network)
    {}

    size_t get_mtu_size() const override {
        return 200;
    }

    constexpr static uint8_t BROADCAST[6]{0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

    int send(uint8_t *msg, size_t msg_size, const uint8_t *mac) override {
        // TODO: Decide about confirm parameter policy
        bool confirm = false;

        if (memcmp(mac, BROADCAST, sizeof(BROADCAST)) == 0) {
            return _network->sendBroadcastMessage(msg, msg_size);
        }
        return _network->sendUnicastMessage(msg, msg_size, mac, confirm);
    }

private:
    ZHNetwork *_network;
};

class QueueSender : public ISender {
public:
    explicit QueueSender() : msgbuf() {
        msgbuf = xMessageBufferCreate(1024);
    }

    size_t get_mtu_size() const override {
        return 200;
    }

    int send(uint8_t *msg, size_t msg_size, const uint8_t *mac) override {
        xMessageBufferSend(msgbuf, mac, 6, portMAX_DELAY);
        return xMessageBufferSend(msgbuf, msg, msg_size, portMAX_DELAY);
    }

    int receive(uint8_t *msg, size_t msg_size, uint8_t *mac) {
        size_t bytes = xMessageBufferReceive(msgbuf, mac, 6, pdMS_TO_TICKS(10));
        if (bytes == 0)
            return 0;
        return xMessageBufferReceive(msgbuf, msg, msg_size, portMAX_DELAY);
    }
private:
    MessageBufferHandle_t msgbuf;
};

#ifdef SENDER_UNSET_TAG
#undef TAG
#endif

#endif /* __SENDER_HPP__ */
