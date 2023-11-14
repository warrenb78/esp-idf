#include "ZHNetwork.h"

#include "mesh_light.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "sender.hpp"
#include "command.hpp"

#define TAG "zh"

ZHNetwork network;

extern QueueSender g_sender;
static esp_netif_t *netif_sta = NULL;

uint8_t buf[255];
uint8_t mac[6];

void zh_task(void *arg)
{
    while (true) {
        TickType_t start_maintenance = xTaskGetTickCount();
        network.maintenance();
        TickType_t end_maintenance = xTaskGetTickCount();
        uint8_t bytes = g_sender.receive(buf, sizeof(buf), mac);
        TickType_t found_bytes = xTaskGetTickCount();
        // ESP_LOGI(TAG, "found %d bytes to send", bytes);
        if (bytes != 0) {
            network.sendUnicastMessage(buf, bytes, mac);
        }
        // if (bytes == 0) {
        //     // ESP_LOGI(TAG, "are you sleeping?!");
        //     vTaskDelay(pdMS_TO_TICKS(10));
        // }
        TickType_t treating_bytes = xTaskGetTickCount();
        // ESP_LOGI(TAG, "%ld %ld %ld %ld", start_maintenance, end_maintenance, found_bytes, treating_bytes);
    }
}

void on_zhrecv_message(const uint8_t *message, uint8_t size, const uint8_t *src_mac) {
    ESP_LOGI(TAG, "Recv message from " MACSTR ", msg size %u", MAC2STR(src_mac), size);
    handle_message(reinterpret_cast<const mesh_addr_t *>(src_mac), message, size);
}

void zh_init()
{
    /*  tcpip initialization */
    ESP_ERROR_CHECK(esp_netif_init());
    /*  event initialization */
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    /*  create network interfaces for mesh (only station instance saved for further manipulation, soft AP instance ignored */
    ESP_ERROR_CHECK(esp_netif_create_default_wifi_mesh_netifs(&netif_sta, NULL));
    /*  wifi initialization */
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&config));
    // ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    unsigned char mac_base[6] = {0};
    esp_efuse_mac_get_default(mac_base);
    esp_read_mac(mac_base, ESP_MAC_WIFI_STA);
    ESP_LOGI(TAG, "mac local base:" MACSTR, MAC2STR(mac_base));

    network.begin();
    network.setOnUnicastReceivingCallback(on_zhrecv_message);
    network.setOnBroadcastReceivingCallback(on_zhrecv_message);
    xTaskCreate(zh_task, "zh_task", 8192, NULL, 10, NULL);
    xTaskCreate(keep_alive_task, "MPKeepAlive", 3072, nullptr, 5, nullptr);
}

