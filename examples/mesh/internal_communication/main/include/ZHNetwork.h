#ifndef ZHNETWORK_H
#define ZHNETWORK_H
#define ESP32
// #include "Arduino.h"
#include <queue>
#include <list>
#include "stdint.h"
#include <functional>
#include <string>
#include <cstring>
#if defined(ESP8266)
#include "ESP8266WiFi.h"
#include "espnow.h"
#endif
#if defined(ESP32)
// #include "WiFi.h"
#include "esp_wifi.h"
#include "esp_now.h"
#endif

#include <memory_pool.hpp>

// #define PRINT_LOG // Uncomment to display to serial port the full operation log.

constexpr static size_t NET_NAME_SIZE = 20;

typedef struct
{
    uint8_t messageType{0};
    uint8_t messageSize{0};
    uint16_t messageID{0};
    char netName[NET_NAME_SIZE]{0};
    uint8_t originalTargetMAC[6]{0};
    uint8_t originalSenderMAC[6]{0};
    uint8_t message[200]{0};
} transmitted_data_t;

typedef struct
{
    uint8_t intermediateTargetMAC[6]{0};
    transmitted_data_t transmittedData;
} outgoing_data_t;

typedef struct
{
    uint8_t intermediateSenderMAC[6]{0};
    transmitted_data_t transmittedData;
} incoming_data_t;

static_assert(sizeof(outgoing_data_t) < 256, "Message too large");
static_assert(sizeof(incoming_data_t) < 256, "Message too large");

typedef struct
{
    uint64_t time{0};
    uint8_t intermediateTargetMAC[6]{0};
    transmitted_data_t transmittedData;
} waiting_data_t;

typedef struct
{
    uint8_t originalTargetMAC[6]{0};
    uint8_t intermediateTargetMAC[6]{0};
} routing_table_t;

typedef struct
{
    uint16_t messageID{0};
    char empty[198]{0}; // Just only to prevent compiler warnings.
} confirmation_id_t;

typedef struct
{
    uint64_t time{0};
    uint8_t targetMAC[6]{0};
    uint16_t messageID{0};
} confirmation_waiting_data_t;

typedef enum
{
    BROADCAST = 1,
    UNICAST,
    UNICAST_WITH_CONFIRM,
    DELIVERY_CONFIRM_RESPONSE,
    SEARCH_REQUEST,
    SEARCH_RESPONSE
} message_type_t;

typedef enum // Just for further development.
{
    SUCCESS = 1,
    ERROR = 0
} error_code_t;

using outgoing_data_elem = DefPool::uptr<outgoing_data_t>;
using incoming_data_elem = DefPool::uptr<incoming_data_t>;
using waiting_data_elem = DefPool::uptr<waiting_data_t>;

typedef std::function<void(const uint8_t *, uint8_t, const uint8_t *)> on_message_t;
typedef std::function<void(const uint8_t *, const uint16_t, const bool)> on_confirm_t;
typedef std::vector<routing_table_t> routing_vector_t;
typedef std::vector<confirmation_waiting_data_t> confirmation_vector_t;
typedef std::queue<outgoing_data_elem, std::list<outgoing_data_elem>> outgoing_queue_t;
//typedef std::queue<incoming_data_elem, std::list<incoming_data_elem>> incoming_queue_t;
//typedef std::queue<waiting_data_elem, std::list<waiting_data_elem>> waiting_queue_t;
typedef std::queue<incoming_data_t> incoming_queue_t;
typedef std::queue<waiting_data_t> waiting_queue_t;

class ZHNetwork
{
public:
    ZHNetwork &setOnBroadcastReceivingCallback(on_message_t onBroadcastReceivingCallback);
    ZHNetwork &setOnUnicastReceivingCallback(on_message_t onUnicastReceivingCallback);
    ZHNetwork &setOnConfirmReceivingCallback(on_confirm_t onConfirmReceivingCallback);

    error_code_t begin(const char *netName = "", const bool gateway = false);

    uint16_t sendBroadcastMessage(const uint8_t *data, uint8_t size);
    uint16_t sendUnicastMessage(const uint8_t *data, uint8_t size, const uint8_t *target, const bool confirm = false);

    void maintenance(void);

    std::string getNodeMac(void);
    std::string getFirmwareVersion(void);
    std::string readErrorCode(error_code_t code); // Just for further development.

    static std::string macToString(const uint8_t *mac);
    uint8_t *stringToMac(const std::string &string, uint8_t *mac);

    error_code_t setCryptKey(const char *key = "");
    error_code_t setMaxNumberOfAttempts(const uint8_t maxNumberOfAttempts);
    uint8_t getMaxNumberOfAttempts(void);
    error_code_t setMaxWaitingTimeBetweenTransmissions(const uint8_t maxWaitingTimeBetweenTransmissions);
    uint8_t getMaxWaitingTimeBetweenTransmissions(void);
    error_code_t setMaxWaitingTimeForRoutingInfo(const uint16_t maxTimeForRoutingInfoWaiting);
    uint16_t getMaxWaitingTimeForRoutingInfo(void);

private:
    static routing_vector_t routingVector;
    static confirmation_vector_t confirmationVector;
    static incoming_queue_t queueForIncomingData;
    static outgoing_queue_t queueForOutgoingData;
    static waiting_queue_t queueForRoutingVectorWaiting;

    static bool criticalProcessSemaphore;
    static bool sentMessageSemaphore;
    static bool confirmReceivingSemaphore;
    static bool confirmReceiving;
    static uint8_t localMAC[6];
    static uint16_t lastMessageID[10];
    static char netName_[NET_NAME_SIZE];
    static char key_[20];

    const char *firmware{"1.42"};
    const uint8_t broadcastMAC[6]{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t maxNumberOfAttempts_{3};
    uint8_t maxWaitingTimeBetweenTransmissions_{5};
    uint8_t numberOfAttemptsToSend{1};
    uint16_t maxTimeForRoutingInfoWaiting_{500};
    uint32_t lastMessageSentTime{0};
    DefPool &pool_ = DefPool::get_pool();

#if defined(ESP8266)
    static void onDataSent(uint8_t *mac, uint8_t status);
    static void onDataReceive(uint8_t *mac, uint8_t *data, uint8_t length);
#endif
#if defined(ESP32)
    static void onDataSent(const uint8_t *mac, esp_now_send_status_t status);
    static void onDataReceive(const esp_now_recv_info_t *mac, const uint8_t *data, int length);
#endif
    uint16_t broadcastMessage(const uint8_t *data, uint8_t size, const uint8_t *target, message_type_t type);
    uint16_t unicastMessage(const uint8_t *data, uint8_t size, const uint8_t *target, const uint8_t *sender, message_type_t type);
    on_message_t onBroadcastReceivingCallback;
    on_message_t onUnicastReceivingCallback;
    on_confirm_t onConfirmReceivingCallback;

protected:
};

#endif
