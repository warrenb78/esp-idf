#ifndef __SENDER_HPP__
#define __SENDER_HPP__

#include <stdint.h>

#include <esp_mesh.h>
#include <esp_mac.h>

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

    int send(uint8_t *msg, size_t msg_size, const uint8_t *mac) override {
        // TODO: Decide about confirm parameter policy
        // TODO: Use msg_size once zh is fixed
        // TODO: Remove the char cast of message once zh is fixed.
        (void)msg_size;
        bool confirm = false;

        return _network->sendUnicastMessage(reinterpret_cast<char *>(msg), mac, confirm);
    }

private:
    ZHNetwork *_network;
};

#ifdef SENDER_UNSET_TAG
#undef TAG
#endif

#endif /* __SENDER_HPP__ */
