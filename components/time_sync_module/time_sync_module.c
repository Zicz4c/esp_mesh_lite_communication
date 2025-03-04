#include "time_sync_module.h"

static const char *TAG = "time_sync_module";

int index_of_node(mac_addr_t * nodes, size_t nodes_size, mac_addr_t node_id){
    for (size_t i = 0; i < nodes_size; i++)
    {
        if(equal_mac(nodes[i], node_id)){
            return i;
        }
    }
    return -1;
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