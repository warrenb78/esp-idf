#include "ZHNetwork.h"

#include "esp_wifi_types.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_random.h"
#include "esp_log.h"

#define TAG "ZHNetwork"
// #define PRINT_LOG

#define MAX_LAYERS 5

routing_vector_t ZHNetwork::routingVector;
confirmation_vector_t ZHNetwork::confirmationVector;
incoming_queue_t ZHNetwork::queueForIncomingData;
outgoing_queue_t ZHNetwork::queueForOutgoingData;
waiting_queue_t ZHNetwork::queueForRoutingVectorWaiting;

bool ZHNetwork::criticalProcessSemaphore{false};
bool ZHNetwork::sentMessageSemaphore{false};
bool ZHNetwork::confirmReceivingSemaphore{false};
bool ZHNetwork::confirmReceiving{false};
char ZHNetwork::netName_[NET_NAME_SIZE]{0};
char ZHNetwork::key_[20]{0};
uint8_t ZHNetwork::localMAC[6]{0};
uint16_t ZHNetwork::lastMessageID[10]{0};

namespace detail {
bool compare_mac(const uint8_t (&lhs)[6], const uint8_t (&rhs)[6]) {
    return memcmp(lhs, rhs, sizeof(lhs)) == 0;
}
} // namespace detail

int random(int max)
{
    return esp_random() % max;
}

ZHNetwork &ZHNetwork::setOnBroadcastReceivingCallback(on_message_t onBroadcastReceivingCallback)
{
    this->onBroadcastReceivingCallback = onBroadcastReceivingCallback;
    return *this;
}

ZHNetwork &ZHNetwork::setOnUnicastReceivingCallback(on_message_t onUnicastReceivingCallback)
{
    this->onUnicastReceivingCallback = onUnicastReceivingCallback;
    return *this;
}

ZHNetwork &ZHNetwork::setOnConfirmReceivingCallback(on_confirm_t onConfirmReceivingCallback)
{
    this->onConfirmReceivingCallback = onConfirmReceivingCallback;
    return *this;
}

error_code_t ZHNetwork::begin(const char *netName, const bool gateway)
{
#if defined(ESP8266)
    randomSeed(os_random());
#endif
#if defined(ESP32)
    // randomSeed(esp_random());
#endif
    if (strlen(netName) >= 1 && strlen(netName) <= NET_NAME_SIZE)
        strcpy(netName_, netName);
#ifdef PRINT_LOG
    // Serial.begin(115200);
#endif
    esp_wifi_set_mode(gateway ? WIFI_MODE_AP : WIFI_MODE_STA);
    // WiFi.mode(gateway ? WIFI_AP_STA : WIFI_STA);
    esp_now_init();
#if defined(ESP8266)
    wifi_get_macaddr(gateway ? SOFTAP_IF : STATION_IF, localMAC);
    esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
#endif
#if defined(ESP32)
    esp_wifi_get_mac(gateway ? (wifi_interface_t)ESP_IF_WIFI_AP : (wifi_interface_t)ESP_IF_WIFI_STA, localMAC);
#endif
    esp_now_register_send_cb(onDataSent);
    esp_now_register_recv_cb(onDataReceive);
    return SUCCESS;
}

uint16_t ZHNetwork::sendBroadcastMessage(const uint8_t *data, uint8_t size)
{
    return broadcastMessage(data, size, broadcastMAC, BROADCAST);
}

uint16_t ZHNetwork::sendUnicastMessage(const uint8_t *data, uint8_t size, const uint8_t *target, const bool confirm)
{
    return unicastMessage(data, size, target, localMAC, confirm ? UNICAST_WITH_CONFIRM : UNICAST);
}

void ZHNetwork::maintenance()
{
    if (sentMessageSemaphore && confirmReceivingSemaphore)
    {
        sentMessageSemaphore = false;
        confirmReceivingSemaphore = false;
        if (confirmReceiving)
        {
#ifdef PRINT_LOG
            ESP_LOGI(TAG, "OK.");
#endif
            outgoing_data_elem outgoingDataElem = std::move(queueForOutgoingData.front());
            outgoing_data_t &outgoingData = *outgoingDataElem;
            queueForOutgoingData.pop();
#if defined(ESP32)
            esp_now_del_peer(outgoingData.intermediateTargetMAC);
#endif
            if (onConfirmReceivingCallback && detail::compare_mac(outgoingData.transmittedData.originalSenderMAC, localMAC) && outgoingData.transmittedData.messageType == BROADCAST)
                onConfirmReceivingCallback(outgoingData.transmittedData.originalTargetMAC, outgoingData.transmittedData.messageID, true);
            if (detail::compare_mac(outgoingData.transmittedData.originalSenderMAC, localMAC) && outgoingData.transmittedData.messageType == UNICAST_WITH_CONFIRM)
            {
                confirmation_waiting_data_t confirmationData;
                confirmationData.time = pdTICKS_TO_MS(xTaskGetTickCount());
                memcpy(&confirmationData.targetMAC, &outgoingData.transmittedData.originalTargetMAC, 6);
                memcpy(&confirmationData.messageID, &outgoingData.transmittedData.messageID, 2);
                confirmationVector.push_back(confirmationData);
            }
        }
        else
        {
#ifdef PRINT_LOG
            ESP_LOGI(TAG, "FAULT.");
#endif
            if (numberOfAttemptsToSend < maxNumberOfAttempts_)
                ++numberOfAttemptsToSend;
            else
            {
                outgoing_data_elem outgoingDataElem = std::move(queueForOutgoingData.front());
                outgoing_data_t &outgoingData = *outgoingDataElem;
                queueForOutgoingData.pop();
#if defined(ESP32)
                esp_now_del_peer(outgoingData.intermediateTargetMAC);
#endif
                numberOfAttemptsToSend = 1;
                for (uint16_t i{0}; i < routingVector.size(); ++i)
                {
                    routing_table_t routingTable = routingVector[i];
                    if (detail::compare_mac(routingTable.originalTargetMAC, outgoingData.transmittedData.originalTargetMAC))
                    {
                        routingVector.erase(routingVector.begin() + i);
#ifdef PRINT_LOG
                        ESP_LOGI(TAG, "CHECKING ROUTING TABLE... Routing to MAC %s deleted", 
                                macToString(outgoingData.transmittedData.originalTargetMAC).c_str());
#endif
                    }
                }
                waiting_data_elem waitingDataElem = pool_.take<waiting_data_t>();
                if (waitingDataElem == nullptr) {
                    ESP_LOGW(TAG, "Drop waiting route table because pool is empty");
                } else {
                    waiting_data_t &waitingData = *waitingDataElem;
                    waitingData.time = pdTICKS_TO_MS(xTaskGetTickCount());
                    memcpy(&waitingData.intermediateTargetMAC, &outgoingData.intermediateTargetMAC, 6);
                    memcpy(&waitingData.transmittedData, &outgoingData.transmittedData, sizeof(transmitted_data_t));
                    queueForRoutingVectorWaiting.push(std::move(waitingDataElem));
                    uint8_t empty{};
                    broadcastMessage(&empty, 0, outgoingData.transmittedData.originalTargetMAC, SEARCH_REQUEST);
                }
            }
        }
    }
    if (!queueForOutgoingData.empty() && !sentMessageSemaphore)
    {
        outgoing_data_elem &outgoingData = queueForOutgoingData.front();
#if defined(ESP32)
        esp_now_peer_info_t peerInfo;
        memset(&peerInfo, 0, sizeof(peerInfo));
        memcpy(peerInfo.peer_addr, outgoingData->intermediateTargetMAC, 6);
        peerInfo.channel = 1;
        peerInfo.encrypt = false;
        esp_now_add_peer(&peerInfo);
#endif
        esp_now_send(outgoingData->intermediateTargetMAC, (uint8_t *)&outgoingData->transmittedData, sizeof(transmitted_data_t));
        lastMessageSentTime = pdTICKS_TO_MS(xTaskGetTickCount());
        sentMessageSemaphore = true;
#ifdef PRINT_LOG
        std::string messageType;
        switch (outgoingData->transmittedData.messageType)
        {
        case BROADCAST:
            messageType = "BROADCAST";
            break;
        case UNICAST:
            messageType = "UNICAST";
            break;
        case UNICAST_WITH_CONFIRM:
            messageType = "UNICAST_WITH_CONFIRM";
            break;
        case DELIVERY_CONFIRM_RESPONSE:
            messageType = "DELIVERY_CONFIRM_RESPONSE";
            break;
        case SEARCH_REQUEST:
            messageType = "SEARCH_REQUEST";
            break;
        case SEARCH_RESPONSE:
            messageType = "SEARCH_RESPONSE";
            break;
        default:
            break;
        }
        ESP_LOGI(TAG, "%s message from MAC %s to MAC %s via MAC %s sended. Status ",
                messageType.c_str(),
                macToString(outgoingData->transmittedData.originalSenderMAC).c_str(), 
                macToString(outgoingData->transmittedData.originalTargetMAC).c_str(),
                macToString(outgoingData->intermediateTargetMAC).c_str());
#endif
    }
    if (!queueForIncomingData.empty())
    {
        criticalProcessSemaphore = true;
        incoming_data_elem incomingDataElem = std::move(queueForIncomingData.front());
        queueForIncomingData.pop();
        if (incomingDataElem == nullptr)
            abort();
        incoming_data_t &incomingData = *incomingDataElem;
        criticalProcessSemaphore = false;
        bool forward{false};
        bool routingUpdate{false};
        // ESP_LOGI(TAG, "got %u %p " MACSTR " " MACSTR, incomingData.transmittedData.messageType, incomingDataElem.get(),
        //         MAC2STR(incomingData.transmittedData.originalTargetMAC), MAC2STR(localMAC));
        switch (incomingData.transmittedData.messageType)
        {
        case BROADCAST:
#ifdef PRINT_LOG
            ESP_LOGI(TAG, "BROADCAST message from MAC %s received with ttl = %d.",
                    macToString(incomingData.transmittedData.originalSenderMAC).c_str(),
                    incomingData.transmittedData.ttl);
#endif
            if (onBroadcastReceivingCallback)
            {
                if (key_[0])
                    for (uint8_t i{0}; i < incomingData.transmittedData.messageSize; ++i)
                        incomingData.transmittedData.message[i] = incomingData.transmittedData.message[i] ^ key_[i % strlen(key_)];
                onBroadcastReceivingCallback(incomingData.transmittedData.message, incomingData.transmittedData.messageSize,
                                             incomingData.transmittedData.originalSenderMAC);
            }
            if(incomingData.transmittedData.ttl) {
                incomingData.transmittedData.ttl--;
                forward = true;
            }
            break;
        case UNICAST:
#ifdef PRINT_LOG
            ESP_LOGI(TAG, "UNICAST message from MAC %s to MAC %s via MAC %s received with ttl = %d.",
                    macToString(incomingData.transmittedData.originalSenderMAC).c_str(),
                    macToString(incomingData.transmittedData.originalTargetMAC).c_str(),
                    macToString(incomingData.intermediateSenderMAC).c_str(),
                    incomingData.transmittedData.ttl);
#endif
            if (detail::compare_mac(incomingData.transmittedData.originalTargetMAC, localMAC))
            {
                if (onUnicastReceivingCallback)
                {
                    if (key_[0])
                        for (uint8_t i{0}; i < incomingData.transmittedData.messageSize; ++i)
                            incomingData.transmittedData.message[i] = incomingData.transmittedData.message[i] ^ key_[i % strlen(key_)];
                    onUnicastReceivingCallback(incomingData.transmittedData.message, incomingData.transmittedData.messageSize,
                                               incomingData.transmittedData.originalSenderMAC);
                }
            }
            else {
                auto &transmittedData = incomingData.transmittedData;
                unicastMessage(transmittedData.message, transmittedData.messageSize, transmittedData.originalTargetMAC, transmittedData.originalSenderMAC, UNICAST);
            }
            break;
        case UNICAST_WITH_CONFIRM:
#ifdef PRINT_LOG
            ESP_LOGI(TAG, "UNICAST_WITH_CONFIRM message from MAC %s to MAC %s via MAC %s received.",
                    macToString(incomingData.transmittedData.originalSenderMAC).c_str(),
                    macToString(incomingData.transmittedData.originalTargetMAC).c_str(),
                    macToString(incomingData.intermediateSenderMAC).c_str());
#endif
            if (detail::compare_mac(incomingData.transmittedData.originalTargetMAC, localMAC))
            {
                if (onUnicastReceivingCallback)
                {
                    if (key_[0])
                        for (uint8_t i{0}; i < incomingData.transmittedData.messageSize; ++i)
                            incomingData.transmittedData.message[i] = incomingData.transmittedData.message[i] ^ key_[i % strlen(key_)];
                    onUnicastReceivingCallback(incomingData.transmittedData.message, incomingData.transmittedData.messageSize, 
                                               incomingData.transmittedData.originalSenderMAC);
                }
                confirmation_id_t id;
                memcpy(&id.messageID, &incomingData.transmittedData.messageID, 2);
                uint8_t temp[sizeof(transmitted_data_t::message)];
                memcpy(&temp, &id, sizeof(transmitted_data_t::message));
                unicastMessage(temp, sizeof(id), incomingData.transmittedData.originalSenderMAC, localMAC, DELIVERY_CONFIRM_RESPONSE);
            }
            else {
                auto &transmittedData = incomingData.transmittedData;
                unicastMessage(transmittedData.message, transmittedData.messageSize, transmittedData.originalTargetMAC, transmittedData.originalSenderMAC, UNICAST_WITH_CONFIRM);
            }
            break;
        case DELIVERY_CONFIRM_RESPONSE:
#ifdef PRINT_LOG
            ESP_LOGI(TAG, "DELIVERY_CONFIRM_RESPONSE message from MAC %s to MAC %s via MAC %s received.",
                    macToString(incomingData.transmittedData.originalSenderMAC).c_str(),
                    macToString(incomingData.transmittedData.originalTargetMAC).c_str(),
                    macToString(incomingData.intermediateSenderMAC).c_str());
#endif
            if (detail::compare_mac(incomingData.transmittedData.originalTargetMAC, localMAC))
            {
                if (onConfirmReceivingCallback)
                {
                    confirmation_id_t id;
                    memcpy(&id.messageID, &incomingData.transmittedData.message, 2);
                    for (uint16_t i{0}; i < confirmationVector.size(); ++i)
                    {
                        confirmation_waiting_data_t confirmationData = confirmationVector[i];
                        if (confirmationData.messageID == id.messageID)
                            confirmationVector.erase(confirmationVector.begin() + i);
                    }
                    onConfirmReceivingCallback(incomingData.transmittedData.originalSenderMAC, id.messageID, true);
                }
            }
            else {
                auto &transmittedData = incomingData.transmittedData;
                unicastMessage(transmittedData.message, transmittedData.messageSize, transmittedData.originalTargetMAC, transmittedData.originalSenderMAC, DELIVERY_CONFIRM_RESPONSE);
            }
            break;
        case SEARCH_REQUEST:
#ifdef PRINT_LOG
            ESP_LOGI(TAG, "SEARCH_REQUEST message from MAC %s to MAC %s received.", 
                    macToString(incomingData.transmittedData.originalSenderMAC).c_str(),
                    macToString(incomingData.transmittedData.originalTargetMAC).c_str());
#endif
            if (detail::compare_mac(incomingData.transmittedData.originalTargetMAC, localMAC)) {
                uint8_t empty{};
                broadcastMessage(&empty, 0, incomingData.transmittedData.originalSenderMAC, SEARCH_RESPONSE);
            } else {
                if(incomingData.transmittedData.ttl) {
                    incomingData.transmittedData.ttl--;
                    forward = true;
                }
            }
            routingUpdate = true;
            break;
        case SEARCH_RESPONSE:
#ifdef PRINT_LOG
            ESP_LOGI(TAG,"SEARCH_RESPONSE message from MAC %s to MAC %s received.",
                    macToString(incomingData.transmittedData.originalSenderMAC).c_str(),
                    macToString(incomingData.transmittedData.originalTargetMAC).c_str());
#endif
            if (!detail::compare_mac(incomingData.transmittedData.originalTargetMAC, localMAC)) {
                if(incomingData.transmittedData.ttl) {
                    incomingData.transmittedData.ttl--;
                    forward = true;
                }
            }
            routingUpdate = true;
            break;
        default:
            break;
        }
        if (forward)
        {
            outgoing_data_elem outgoingData = pool_.take<outgoing_data_t>();
            if (outgoingData == nullptr) {
                ESP_LOGW(TAG, "Drop packet, pool empty");
            } else {
                memcpy(&outgoingData->transmittedData, &incomingData.transmittedData, sizeof(transmitted_data_t));
                memcpy(&outgoingData->intermediateTargetMAC, &broadcastMAC, 6);
                queueForOutgoingData.push(std::move(outgoingData));
            }
        }
        if (routingUpdate)
        {
            bool routeFound{false};
            for (uint16_t i{0}; i < routingVector.size(); ++i)
            {
                routing_table_t routingTable = routingVector[i];
                if (detail::compare_mac(routingTable.originalTargetMAC, incomingData.transmittedData.originalSenderMAC))
                {
                    routeFound = true;
                    if (!detail::compare_mac(routingTable.intermediateTargetMAC, incomingData.intermediateSenderMAC))
                    {
                        memcpy(&routingTable.intermediateTargetMAC, &incomingData.intermediateSenderMAC, 6);
                        routingVector.at(i) = routingTable;
#ifdef PRINT_LOG
                        ESP_LOGI(TAG, "CHECKING ROUTING TABLE... Routing to MAC %s updated. Target is %s.",
                                macToString(incomingData.transmittedData.originalSenderMAC).c_str(),
                                macToString(incomingData.intermediateSenderMAC).c_str());
#endif
                    }
                }
            }
            if (!routeFound)
            {
                if (!detail::compare_mac(incomingData.transmittedData.originalSenderMAC, incomingData.intermediateSenderMAC))
                {
                    routing_table_t routingTable;
                    memcpy(&routingTable.originalTargetMAC, &incomingData.transmittedData.originalSenderMAC, 6);
                    memcpy(&routingTable.intermediateTargetMAC, &incomingData.intermediateSenderMAC, 6);
                    routingVector.push_back(routingTable);
#ifdef PRINT_LOG
                    ESP_LOGI(TAG, "CHECKING ROUTING TABLE... Routing to MAC %s added. Target is %s.",
                            macToString(incomingData.transmittedData.originalSenderMAC).c_str(),
                            macToString(incomingData.intermediateSenderMAC).c_str());
#endif
                }
            }
        }
    }
    if (!queueForRoutingVectorWaiting.empty())
    {
        // Taking reference to unique_ptr, handle it carefully.
        waiting_data_elem &waitingDataElemRef = queueForRoutingVectorWaiting.front();
        waiting_data_elem waitingDataElem; // Init to null will take ownership later.
        waiting_data_t &waitingData = *waitingDataElemRef;
        for (uint16_t i{0}; i < routingVector.size(); ++i)
        {
            routing_table_t routingTable = routingVector[i];
            if (detail::compare_mac(routingTable.originalTargetMAC, waitingData.transmittedData.originalTargetMAC))
            {
                waitingDataElem = std::move(waitingDataElemRef);
                queueForRoutingVectorWaiting.pop();
                outgoing_data_elem outgoingData = pool_.take<outgoing_data_t>();
                if (outgoingData == nullptr) {
                    ESP_LOGW(TAG, "Droping waiting for routing vector msg pool empty");
                    return;
                }

                memcpy(&outgoingData->transmittedData, &waitingData.transmittedData, sizeof(transmitted_data_t));
                memcpy(&outgoingData->intermediateTargetMAC, &routingTable.intermediateTargetMAC, 6);
#ifdef PRINT_LOG
                    ESP_LOGI(TAG, "CHECKING ROUTING TABLE... Routing to MAC %s found. Target is %s.",
                            macToString(outgoingData->transmittedData.originalTargetMAC).c_str(),
                            macToString(outgoingData->intermediateTargetMAC).c_str());
#endif
                queueForOutgoingData.push(std::move(outgoingData));
                return;
            }
        }
        if ((pdTICKS_TO_MS(xTaskGetTickCount()) - waitingData.time) > maxTimeForRoutingInfoWaiting_)
        {
            waitingDataElem = std::move(waitingDataElemRef);
            queueForRoutingVectorWaiting.pop();
#ifdef PRINT_LOG
            ESP_LOGI(TAG, "CHECKING ROUTING TABLE... Routing to MAC %s not found.",
                    macToString(waitingData.transmittedData.originalTargetMAC).c_str());
            std::string messageType;
            switch (waitingData.transmittedData.messageType)
            {
            case UNICAST:
                messageType = "UNICAST";
                break;
            case UNICAST_WITH_CONFIRM:
                messageType = "UNICAST_WITH_CONFIRM";
                break;
            case DELIVERY_CONFIRM_RESPONSE:
                messageType = "DELIVERY_CONFIRM_RESPONSE";
                break;
            default:
                break;
            }
            ESP_LOGI(TAG, "%s message from MAC %s to MAC %s via MAC %s undelivered.",
                    messageType.c_str(),
                    macToString(waitingData.transmittedData.originalSenderMAC).c_str(),
                    macToString(waitingData.transmittedData.originalTargetMAC).c_str(),
                    macToString(waitingData.intermediateTargetMAC).c_str());
#endif
            if (waitingData.transmittedData.messageType == UNICAST_WITH_CONFIRM && detail::compare_mac(waitingData.transmittedData.originalSenderMAC, localMAC))
                if (onConfirmReceivingCallback)
                    onConfirmReceivingCallback(waitingData.transmittedData.originalTargetMAC, waitingData.transmittedData.messageID, false);
        }
    }
    if (confirmationVector.size())
    {
        for (uint16_t i{0}; i < confirmationVector.size(); ++i)
        {
            confirmation_waiting_data_t confirmationData = confirmationVector[i];
            if ((pdTICKS_TO_MS(xTaskGetTickCount()) - confirmationData.time) > maxTimeForRoutingInfoWaiting_)
            {
                confirmationVector.erase(confirmationVector.begin() + i);
                uint8_t empty{};
                broadcastMessage(&empty, 0, confirmationData.targetMAC, SEARCH_REQUEST);
                if (onConfirmReceivingCallback)
                    onConfirmReceivingCallback(confirmationData.targetMAC, confirmationData.messageID, false);
            }
        }
    }
}

std::string ZHNetwork::getNodeMac()
{
    return macToString(localMAC);
}

std::string ZHNetwork::getFirmwareVersion()
{
    return firmware;
}

std::string ZHNetwork::macToString(const uint8_t *mac)
{
    std::string string;
    const char baseChars[16] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
    for (uint32_t i{0}; i < 6; ++i)
    {
        string += (char)*(baseChars + (mac[i] >> 4));
        string += (char)*(baseChars + mac[i] % 16);
    }
    return string;
}

uint8_t *ZHNetwork::stringToMac(const std::string &string, uint8_t *mac)
{
    const uint8_t baseChars[75]{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 0, 0, 0, 0, 0, 0,
                                10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 0, 0, 0, 0, 0, 0,
                                10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35};
    for (uint32_t i = 0; i < 6; ++i)
        mac[i] = (*(baseChars + string[i * 2] - '0') << 4) + *(baseChars + string[i * 2 + 1] - '0');
    return mac;
}

error_code_t ZHNetwork::setCryptKey(const char *key)
{
    if (strlen(key) >= 1 && strlen(key) <= 20)
        strcpy(key_, key);
    return SUCCESS;
}

error_code_t ZHNetwork::setMaxNumberOfAttempts(const uint8_t maxNumberOfAttempts)
{
    if (maxNumberOfAttempts < 1 || maxNumberOfAttempts > 10)
        return ERROR;
    maxNumberOfAttempts_ = maxNumberOfAttempts;
    return SUCCESS;
}

uint8_t ZHNetwork::getMaxNumberOfAttempts()
{
    return maxNumberOfAttempts_;
}

error_code_t ZHNetwork::setMaxWaitingTimeBetweenTransmissions(const uint8_t maxWaitingTimeBetweenTransmissions)
{
    if (maxWaitingTimeBetweenTransmissions < 50 || maxWaitingTimeBetweenTransmissions > 250)
        return ERROR;
    maxWaitingTimeBetweenTransmissions_ = maxWaitingTimeBetweenTransmissions;
    return SUCCESS;
}

uint8_t ZHNetwork::getMaxWaitingTimeBetweenTransmissions()
{
    return maxWaitingTimeBetweenTransmissions_;
}

error_code_t ZHNetwork::setMaxWaitingTimeForRoutingInfo(const uint16_t maxTimeForRoutingInfoWaiting)
{
    if (maxTimeForRoutingInfoWaiting < 500 || maxTimeForRoutingInfoWaiting > 5000)
        return ERROR;
    maxTimeForRoutingInfoWaiting_ = maxTimeForRoutingInfoWaiting;
    return SUCCESS;
}

uint16_t ZHNetwork::getMaxWaitingTimeForRoutingInfo()
{
    return maxTimeForRoutingInfoWaiting_;
}

#if defined(ESP8266)
void IRAM_ATTR ZHNetwork::onDataSent(uint8_t *mac, uint8_t status)
#endif
#if defined(ESP32)
void IRAM_ATTR ZHNetwork::onDataSent(const uint8_t *mac, esp_now_send_status_t status)
#endif
{
    confirmReceivingSemaphore = true;
    confirmReceiving = status ? false : true;
}

#if defined(ESP8266)
void IRAM_ATTR ZHNetwork::onDataReceive(uint8_t *mac, uint8_t *data, uint8_t length)
#endif
#if defined(ESP32)
void IRAM_ATTR ZHNetwork::onDataReceive(const esp_now_recv_info_t *mac, const uint8_t *data, int length)
#endif
{
    if (criticalProcessSemaphore)
        return;
    criticalProcessSemaphore = true;
    if (length != sizeof(transmitted_data_t))
    {
        criticalProcessSemaphore = false;
        return;
    }
    incoming_data_elem incomingDataElem = DefPool::get_pool().take<incoming_data_t>();
    if (incomingDataElem == nullptr) {
        ESP_LOGE(TAG, "Failed to receive packet from wire level pool empty len %u", length);
        return;
    }
    incoming_data_t &incomingData = *incomingDataElem;
    memcpy(&incomingData.transmittedData, data, sizeof(transmitted_data_t));
    if (detail::compare_mac(incomingData.transmittedData.originalSenderMAC, localMAC))
    {
        criticalProcessSemaphore = false;
        return;
    }
    if (netName_[0] != '\0')
    {
        if (strncmp(incomingData.transmittedData.netName, netName_, sizeof(netName_)) != 0)
        {
            criticalProcessSemaphore = false;
            return;
        }
    }
    for (uint8_t i{0}; i < sizeof(lastMessageID) / 2; ++i)
        if (lastMessageID[i] == incomingData.transmittedData.messageID)
        {
            criticalProcessSemaphore = false;
            return;
        }
    for (uint8_t i{sizeof(lastMessageID) / 2 - 1}; i >= 1; --i)
        lastMessageID[i] = lastMessageID[i - 1];
    lastMessageID[0] = incomingData.transmittedData.messageID;
    memcpy(&incomingData.intermediateSenderMAC, mac, 6);
    queueForIncomingData.push(std::move(incomingDataElem));
    criticalProcessSemaphore = false;
}

uint16_t ZHNetwork::broadcastMessage(const uint8_t *data, uint8_t size, const uint8_t *target, message_type_t type)
{
    outgoing_data_elem outgoingData = pool_.take<outgoing_data_t>();
    if (outgoingData == nullptr) {
        ESP_LOGW(TAG, "Broadcase failed because of empty pool");
        return ERROR;
    }
    outgoingData->transmittedData.messageType = type;
    outgoingData->transmittedData.messageID = ((uint16_t)random(32767) << 8) | (uint16_t)random(32767);
    outgoingData->transmittedData.ttl = MAX_LAYERS;
    memcpy(&outgoingData->transmittedData.netName, &netName_, NET_NAME_SIZE);
    memcpy(&outgoingData->transmittedData.originalTargetMAC, target, 6);
    memcpy(&outgoingData->transmittedData.originalSenderMAC, &localMAC, 6);
    outgoingData->transmittedData.messageSize = size;
    if (size > sizeof(outgoingData->transmittedData.message))
        return ESP_ERR_INVALID_SIZE;
    memcpy(outgoingData->transmittedData.message, data, size);
    if (key_[0] && outgoingData->transmittedData.messageType == BROADCAST)
        for (uint8_t i{0}; i < outgoingData->transmittedData.messageSize; ++i)
            outgoingData->transmittedData.message[i] = outgoingData->transmittedData.message[i] ^ key_[i % strlen(key_)];
    memcpy(&outgoingData->intermediateTargetMAC, &broadcastMAC, 6);
    outgoing_data_t &res = *outgoingData; // For print log and return res.
    queueForOutgoingData.push(std::move(outgoingData));
#ifdef PRINT_LOG
    std::string messageType;
    switch (res.transmittedData.messageType)
    {
    case BROADCAST:
        messageType = "BROADCAST";
        break;
    case SEARCH_REQUEST:
        messageType = "SEARCH_REQUEST";
        break;
    case SEARCH_RESPONSE:
        messageType = "SEARCH_RESPONSE";
        break;
    default:
        break;
    }
    ESP_LOGI(TAG, "%s message from MAC %s to MAC %s added to queue.",
            messageType.c_str(),
            macToString(res.transmittedData.originalSenderMAC).c_str(),
            macToString(res.transmittedData.originalTargetMAC).c_str());
#endif
    return res.transmittedData.messageID;
}

uint16_t ZHNetwork::unicastMessage(const uint8_t *data, uint8_t size, const uint8_t *target, const uint8_t *sender, message_type_t type)
{
    outgoing_data_elem outgoingDataElem = pool_.take<outgoing_data_t>();
    if (outgoingDataElem == nullptr) {
        ESP_LOGW(TAG, "Failed to send unicast pool empty");
        return ERROR;
    }
    outgoing_data_t &outgoingData = *outgoingDataElem;
    outgoingData.transmittedData.messageType = type;
    outgoingData.transmittedData.messageID = ((uint16_t)random(32767) << 8) | (uint16_t)random(32767);
    outgoingData.transmittedData.ttl = MAX_LAYERS;
    memcpy(&outgoingData.transmittedData.netName, &netName_, NET_NAME_SIZE);
    memcpy(&outgoingData.transmittedData.originalTargetMAC, target, 6);
    memcpy(&outgoingData.transmittedData.originalSenderMAC, sender, 6);
    outgoingData.transmittedData.messageSize = size;
    if (size > sizeof(outgoingData.transmittedData.message))
        return ESP_ERR_INVALID_SIZE;
    memcpy(outgoingData.transmittedData.message, data, size);
    if (key_[0] && detail::compare_mac(outgoingData.transmittedData.originalSenderMAC, localMAC) && outgoingData.transmittedData.messageType != DELIVERY_CONFIRM_RESPONSE)
        for (uint8_t i{0}; i < outgoingData.transmittedData.messageSize; ++i)
            outgoingData.transmittedData.message[i] = outgoingData.transmittedData.message[i] ^ key_[i % strlen(key_)];
    for (uint16_t i{0}; i < routingVector.size(); ++i)
    {
        routing_table_t routingTable = routingVector[i];
        if (memcmp(routingTable.originalTargetMAC, target, sizeof(routingTable.originalTargetMAC)) == 0)
        {
            memcpy(&outgoingData.intermediateTargetMAC, &routingTable.intermediateTargetMAC, 6);
            queueForOutgoingData.push(std::move(outgoingDataElem));
#ifdef PRINT_LOG
            ESP_LOGI(TAG, "CHECKING ROUTING TABLE... Routing to MAC %s found. Target is %s.",
                    macToString(outgoingData.transmittedData.originalTargetMAC).c_str(),
                    macToString(outgoingData.intermediateTargetMAC).c_str());
            std::string messageType;
            switch (outgoingData.transmittedData.messageType)
            {
            case UNICAST:
                messageType = "UNICAST";
                break;
            case UNICAST_WITH_CONFIRM:
                messageType = "UNICAST_WITH_CONFIRM";
                break;
            case DELIVERY_CONFIRM_RESPONSE:
                messageType = "DELIVERY_CONFIRM_RESPONSE";
                break;
            default:
                break;
            }
            ESP_LOGI(TAG, "%s message from MAC %s to MAC %s via MAC %s added to queue.",
                    messageType.c_str(),
                    macToString(outgoingData.transmittedData.originalSenderMAC).c_str(),
                    macToString(outgoingData.transmittedData.originalTargetMAC).c_str(),
                    macToString(outgoingData.intermediateTargetMAC).c_str());
#endif
            return outgoingData.transmittedData.messageID;
        }
    }
    memcpy(&outgoingData.intermediateTargetMAC, target, 6);
    // ESP_LOGI(TAG, "queueForOutgoingData.size() = %d", queueForOutgoingData.size());
    queueForOutgoingData.push(std::move(outgoingDataElem));
#ifdef PRINT_LOG
    ESP_LOGI(TAG, "CHECKING ROUTING TABLE... Routing to MAC %s not found. Target is %s.",
            macToString(outgoingData.transmittedData.originalTargetMAC).c_str(),
            macToString(outgoingData.intermediateTargetMAC).c_str());
    std::string messageType;
    switch (outgoingData.transmittedData.messageType)
    {
    case UNICAST:
        messageType = "UNICAST";
        break;
    case UNICAST_WITH_CONFIRM:
        messageType = "UNICAST_WITH_CONFIRM";
        break;
    case DELIVERY_CONFIRM_RESPONSE:
        messageType = "DELIVERY_CONFIRM_RESPONSE";
        break;
    default:
        break;
    }
    ESP_LOGI(TAG, "%s message from MAC %s to MAC %s via MAC %s added to queue.",
            messageType.c_str(),
            macToString(outgoingData.transmittedData.originalSenderMAC).c_str(),
            macToString(outgoingData.transmittedData.originalTargetMAC).c_str(),
            macToString(outgoingData.intermediateTargetMAC).c_str());
#endif
    return outgoingData.transmittedData.messageID;
}