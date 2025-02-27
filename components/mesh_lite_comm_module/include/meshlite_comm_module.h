#pragma once

#include <esp_err.h>
#include <esp_mesh_lite_espnow.h>
#include <cJSON.h>
#include <esp_mesh_lite.h>
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

/// @brief sends a json message over the native communication protocoll of esp-mesh-lite
/// @param message_type type of the message, receiver needs to have this defined in the action list
/// @param response_type type of the response message, receiver needs to have this defined in the action list
/// @param retries number of retries, when no response is received
/// @param payload pointer to the payload of the message
/// @param send_fn Send function pointer for how the data should be send. Send function should be or inlcude following functions: 
///                 - esp_mesh_lite_send_broadcast_msg_to_child() 
///                 - esp_mesh_lite_send_broadcast_msg_to_parent() 
///                 - esp_mesh_lite_send_msg_to_root() 
///                 - esp_mesh_lite_send_msg_to_parent()
/// @return 
 esp_err_t send_json_message(char * message_type, char * response_type, uint8_t retries, cJSON * payload, esp_err_t (*send_fn)(const char *payload));

/// @brief function for initializing the action list for the native communication protocoll of esp-mesh-lite
/// @param timer FreeRTOS timer for making sure that the actions get subscribed to the msg received event. will be automatically stopped if the registration fails (error or is already registered)
 void init_action(TimerHandle_t timer);

 static esp_mesh_lite_msg_action_t * msg_action = NULL;

 void set_action(esp_mesh_lite_msg_action_t * new_action);

