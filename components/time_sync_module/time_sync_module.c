#include "time_sync_module.h"

static const char *TAG = "time_sync_module";

int index_of_node(mac_addr_t * nodes, size_t * nodes_size, mac_addr_t node_id){
    for (size_t i = 0; i < *nodes_size; i++)
    {
        if(equal_mac(nodes[i], node_id)){
            return i;
        }
    }
    //node not found, add it to array
    nodes[*nodes_size] = node_id;
    int id_index = *nodes_size;
    (*nodes_size)++;
    return id_index;
}  



cJSON * handle_ack(cJSON * payload, uint32_t seq){
    cJSON * data = NULL;
    if(data_queue == NULL){
        return NULL;
    }
    xQueueReceive(data_queue, data, 0);
    cJSON_Delete(data);
    return NULL;
}

cJSON * add_mac_to_json(cJSON * target, mac_addr_t mac){
    cJSON * data_mac = cJSON_AddArrayToObject(target, JSON_MAC);
    for (size_t i = 0; i < MAC_ADDR_SIZE; i++)
    {   
        cJSON * mac_part = cJSON_CreateNumber(mac.addr[i]);
        cJSON_AddItemToArray(data_mac, mac_part);
    }
    return data_mac;
}

mac_addr_t read_mac_from_json(cJSON * json){
    cJSON * mac_arr = cJSON_IsArray(json) ? json : cJSON_GetObjectItem(json, JSON_MAC);
    mac_addr_t mac;
    for (size_t i = 0; i < MAC_ADDR_SIZE; i++)
    {
        mac.addr[i] = (uint8_t)cJSON_GetNumberValue(cJSON_GetArrayItem(mac_arr, i));
    }
    return mac;
}

void get_time_from_json(cJSON * json, int64_t * out_s, int32_t * out_us){
    cJSON *recv_us = cJSON_GetObjectItem(json, JSON_US);
    cJSON *recv_s = cJSON_GetObjectItem(json, JSON_S);

    // get values from json
    *out_us = (int32_t)cJSON_GetNumberValue(recv_us);
    *out_s = (int32_t)cJSON_GetNumberValue(recv_s);
    ESP_LOGI(TAG, "[get_time_from_json] s: %lld | us: %ld", *out_s, *out_us);
}

void handle_delay_second_overflow(delay_t * d_rn){
    if(abs(d_rn->s) == 1){
        int64_t d_us = d_rn->s * 1000000 + d_rn->us;
        if(d_us < 1000000 && d_us > 0){
            d_rn->us = d_us;
            d_rn->s = 0;
        }
    }
}