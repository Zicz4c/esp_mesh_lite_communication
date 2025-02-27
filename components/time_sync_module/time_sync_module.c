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
    return NULL;
}
