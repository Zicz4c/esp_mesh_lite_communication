#include "meshlite_comm_module.h"

 esp_err_t send_json_message(char * message_type, char * response_type, uint8_t retries, cJSON * payload, esp_err_t (*send_fn)(const char *payload)){
    return esp_mesh_lite_try_sending_msg(message_type, response_type, retries, payload, send_fn);
}

 void init_action(TimerHandle_t timer){
    esp_err_t err = esp_mesh_lite_msg_action_list_register(msg_action);
    if(err != ESP_OK){
        xTimerStop(timer, 0);
    }
}

 void set_action(esp_mesh_lite_msg_action_t * new_action){
    msg_action = new_action;
}