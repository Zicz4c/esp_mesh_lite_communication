#include "time_sync_module.h"

esp_err_t add_time_sync_root_action_callbacks();

esp_err_t send_first_sync_time();
cJSON *handle_time_sync_request(cJSON *payload, uint32_t seq);
/// @brief Answer of node after first sync request
/// @param payload updated time of node without regard to any delays
/// @param seq 
/// @return JSON Object:
/// { 
///     "s" : 000000 ,
///     "us": 000000 ,
///     "mac": [ 000, 000, 000, 000, 000, 000 ]
/// }
cJSON * handle_node_first_sync_time(cJSON * payload, uint32_t seq);
/// @brief Answer of node after first sync
/// @param payload time of node regarding calculated delay from root to node
/// @param seq 
/// @return 
cJSON * handle_node_sync_time_w_delay(cJSON * payload, uint32_t seq);
cJSON * handle_node_sync_time(cJSON * payload, uint32_t seq);
