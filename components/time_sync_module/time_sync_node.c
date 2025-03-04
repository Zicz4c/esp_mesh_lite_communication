#include "time_sync_node.h"

static const char *TAG = "time_sync_node";

// node side
#pragma region node_side

esp_mesh_lite_msg_action_t time_sync_node_callbacks[] = {
    {TIME_SYNC_FIRST_MESSAGE, TIME_SYNC_FIRST_MESSAGE_ACK, handle_first_sync_time},
    {TIME_SYNC_FIRST_MESSAGE_ACK, NULL, handle_ack},
    {TIME_SYNC_ROOT_TIME_MESSAGE, TIME_SYNC_ROOT_TIME_MESSAGE_ACK, handle_root_sync_time},
    {TIME_SYNC_ROOT_TIME_MESSAGE_ACK, NULL, handle_ack},
    {TIME_SYNC_ROOT_CORRECTED_TIME_MESSAGE, TIME_SYNC_ROOT_CORRECTED_TIME_MESSAGE_ACK, handle_root_corrected_time},
    {TIME_SYNC_ROOT_CORRECTED_TIME_MESSAGE_ACK, NULL, handle_ack},
    {NULL,NULL,NULL}
};

mac_addr_t own_mac;

esp_err_t add_time_sync_node_action_callbacks(){
    if(data_queue == NULL){
        data_queue = xQueueCreate(100, sizeof(cJSON *));
    }
    esp_wifi_get_mac(ESP_IF_WIFI_STA, own_mac.addr);
    return esp_mesh_lite_msg_action_list_register(time_sync_node_callbacks);
}

cJSON * get_json_item_for_mac(mac_addr_t mac, cJSON * data){
    int arr_size = cJSON_GetArraySize(data);
    int mac_index = -1;
    for(size_t i = 0; i < arr_size && mac_index < 0 ; i++){
        cJSON * item = cJSON_GetArrayItem(data, i);
        cJSON * item_mac = cJSON_GetObjectItem(item, JSON_MAC);
        //ESP_LOGI(TAG, "Item MAC: %s", cJSON_Print(item_mac));
        mac_addr_t item_mac_addr;
        for (size_t j = 0; j < MAC_ADDR_SIZE; j++)
        {
            item_mac_addr.addr[j] = (uint8_t)cJSON_GetNumberValue(cJSON_GetArrayItem(item_mac, j));
        }

        if(equal_mac(mac, item_mac_addr)){
            //ESP_LOGI(TAG, "Found MAC");
            mac_index = i;
            return item;
        }
    }
    return NULL;
}



cJSON * handle_first_sync_time(cJSON * payload, uint32_t seq){
    send_json_message(TIME_SYNC_FIRST_MESSAGE, TIME_SYNC_FIRST_MESSAGE_ACK, 0, payload, esp_mesh_lite_send_broadcast_msg_to_child);
    if(own_mac.addr[0] == 0 ){
        esp_wifi_get_mac(ESP_IF_WIFI_STA, own_mac.addr);
    }
    // data from root
    cJSON * recv_us = cJSON_GetObjectItem(payload, JSON_US);
    cJSON * recv_s = cJSON_GetObjectItem(payload, JSON_S);
    struct timeval t_r;
    t_r.tv_sec = (int)cJSON_GetNumberValue(recv_s);
    t_r.tv_usec = (int)cJSON_GetNumberValue(recv_us);
    
    // set node time equal to root time
    struct timeval t_n;
    settimeofday(&t_r, NULL);
    // create data for message to root
    cJSON * data = cJSON_CreateObject();
    cJSON * message_seq = cJSON_AddNumberToObject(data, JSON_SEQ, seq);
    cJSON * data_mac = add_mac_to_json(data, own_mac);
    gettimeofday(&t_n, NULL); // get node time as late as possible
    cJSON * data_s = cJSON_AddNumberToObject(data, JSON_S, t_n.tv_sec);
    cJSON * data_us = cJSON_AddNumberToObject(data, JSON_US, t_n.tv_usec);
    ESP_LOGI(TAG, "[handle_first_sync_time]Handle first sync message");

    send_json_message(TIME_SYNC_NODE_FIRST_MESSAGE, TIME_SYNC_NODE_FIRST_MESSAGE_ACK, 0,  data, esp_mesh_lite_send_msg_to_root);
    xQueueSend(data_queue, &data, 0);
    return NULL;
}

cJSON * handle_root_sync_time(cJSON * payload, uint32_t seq){ 
    struct timeval t_n;
    gettimeofday(&t_n, NULL); // get node time as early as possible
    // redirect broadcast to children
    send_json_message(TIME_SYNC_ROOT_TIME_MESSAGE, TIME_SYNC_ROOT_TIME_MESSAGE_ACK,
        0, payload, esp_mesh_lite_send_broadcast_msg_to_child);
    // check if payload contains data for this node
    cJSON * item_for_mac = get_json_item_for_mac(own_mac, payload);
    if(item_for_mac == NULL){
        //ESP_LOGI(TAG, "[handle_root_sync_time] own MAC: "MACSTR, MAC2STR(own_mac.addr));
        //ESP_LOGI(TAG, "[handle_root_sync_time] payload %s", cJSON_Print(payload));
        //ESP_LOGE(TAG, "No item for mac found");
        
        // TODO: replace with queue
        //send_json_message(TIME_SYNC_REQUEST, TIME_SYNC_REQUEST_ACK, 0,  NULL, esp_mesh_lite_send_msg_to_root);
        return NULL;
    }

    // data from root
    cJSON * recv_us = cJSON_GetObjectItem(item_for_mac, JSON_US);
    cJSON * recv_s = cJSON_GetObjectItem(item_for_mac, JSON_S);
    struct timeval t_r;
    t_r.tv_sec = (int)cJSON_GetNumberValue(recv_s);
    t_r.tv_usec = (int)cJSON_GetNumberValue(recv_us);
    //ESP_LOGI(TAG, "handle_node_sync_time_w_delay - Time: %lld.%ld : %llu.%06lu", t_r.tv_sec, t_r.tv_usec,t_n.tv_sec, t_n.tv_usec);

    delay_t d_rn;
    d_rn.us = t_r.tv_usec - t_n.tv_usec;
    d_rn.s = t_r.tv_sec - t_n.tv_sec;
    if(d_rn.s == 1){
        int64_t d_us = d_rn.s * 1000000 + d_rn.us;
        if(d_us < 1000000 && d_us > 0){
            d_rn.us = d_us;
            d_rn.s = 0;
        }
    }
    ESP_LOGI(TAG, "[handle_root_sync_time] Delay: %ld.%06ld", d_rn.s, d_rn.us);
    cJSON * data = cJSON_CreateObject();
    
    cJSON_AddNumberToObject(data, JSON_D_RN_S, d_rn.s);
    cJSON_AddNumberToObject(data, JSON_D_RN_US, d_rn.us);
    cJSON * message_seq = cJSON_AddNumberToObject(data, JSON_SEQ, seq);
    //cJSON * data_mac = cJSON_AddArrayToObject(data, JSON_MAC);
    if(own_mac.addr[0] == 0 ){
        esp_wifi_get_mac(ESP_IF_WIFI_STA, own_mac.addr);
    }
    add_mac_to_json(data, own_mac);
    gettimeofday(&t_n, NULL);
    cJSON_AddNumberToObject(data, JSON_S, t_n.tv_sec);
    cJSON_AddNumberToObject(data, JSON_US, t_n.tv_usec);
    send_json_message(TIME_SYNC_WITH_DELAY_MESSAGE, TIME_SYNC_WITH_DELAY_MESSAGE_ACK, 0,  data, esp_mesh_lite_send_msg_to_root);
    xQueueSend(data_queue, &data, 0);
    return NULL;
}

cJSON * handle_root_corrected_time(cJSON * payload, uint32_t seq){
    send_json_message(TIME_SYNC_ROOT_CORRECTED_TIME_MESSAGE, TIME_SYNC_ROOT_CORRECTED_TIME_MESSAGE_ACK,
        0, payload, esp_mesh_lite_send_broadcast_msg_to_child);
    cJSON * item_for_mac = get_json_item_for_mac(own_mac, payload);
    if(item_for_mac == NULL){
        ESP_LOGE(TAG, "[handle_root_corrected_time] No item for mac found");
        ESP_LOGI(TAG, "[handle_root_corrected_time] payload %s", cJSON_Print(payload));

        // TODO: replace with queue
        //send_json_message(TIME_SYNC_REQUEST, TIME_SYNC_REQUEST_ACK, 0,  NULL, esp_mesh_lite_send_msg_to_root);
        return NULL;
    }

    cJSON * recv_us = cJSON_GetObjectItem(item_for_mac, JSON_US);
    cJSON * recv_s = cJSON_GetObjectItem(item_for_mac, JSON_S);
    struct timeval t_r;
    struct timeval t_n;
    t_r.tv_sec = (int)cJSON_GetNumberValue(recv_s);
    t_r.tv_usec = (int)cJSON_GetNumberValue(recv_us);
    settimeofday(&t_r, NULL);
    
    cJSON * data = cJSON_CreateObject() ;

    cJSON * message_seq = cJSON_AddNumberToObject(data, JSON_SEQ, seq);
    //cJSON * data_mac = cJSON_AddArrayToObject(data, JSON_MAC);
    //mac_addr_t mac;
    if(own_mac.addr[0] == 0 ){
        esp_wifi_get_mac(ESP_IF_WIFI_STA, own_mac.addr);
    }
    add_mac_to_json(data, own_mac);
    gettimeofday(&t_n, NULL);
    cJSON_AddNumberToObject(data, JSON_S, t_n.tv_sec);
    cJSON_AddNumberToObject(data, JSON_US, t_n.tv_usec);
    ESP_LOGI(TAG, "[handle_root_corrected_time] Handle corrected time %llu.%06lu", t_n.tv_sec, t_n.tv_usec);
    send_json_message(TIME_SYNC_MESSAGE, TIME_SYNC_MESSAGE_ACK, 0,  data, esp_mesh_lite_send_msg_to_root);
    xQueueSend(data_queue, &data, 0);
    return NULL;
}

#pragma endregion