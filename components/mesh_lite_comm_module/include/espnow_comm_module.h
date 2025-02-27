#pragma once

#include <esp_err.h>
#include <esp_mesh_lite_espnow.h>
#include "esp_log.h"
#include <string.h>

/// @brief register a callback function to be called when a message of a certain type is received. 
/// MUST be called AFTER esp_mesh_lite_init() is called. 
/// esp_mesh_lite_init() is calling esp_mesh_lite_espnow_init(), which initializes the espnow functionality. 
/// @param message_type the type of the message, should be interger between incl. 5 and 200. 1-4 are already used by the system, 200 is ESPNOW_DATA_TYPE_RESERVE
/// @param callback the callback function to be called when a message of the specified type is received
/// @return - ESP_OK: Callback registration successful - ESP_FAIL: Failed to register the callback
esp_err_t register_espnow_message_receive_callback(uint8_t message_type, void (*callback)(const uint8_t *mac_addr, const uint8_t *data, int len));

/// @brief send a message to a specified mac address over espnow
/// @param message_type the type of the message, should be interger between incl. 5 and 200. 1-4 are already used by the system, 200 is ESPNOW_DATA_TYPE_RESERVE.
/// Receiver needs to have a callback registered to this message_type to receive the message
/// @param dst_mac the mac address of the destination device
/// @param payload the data to be sent
/// @param payload_len the length of the data to be sent
void espnow_send_message(uint8_t message_type, uint8_t dst_mac[ESP_NOW_ETH_ALEN], const uint8_t *payload, uint8_t payload_len);

