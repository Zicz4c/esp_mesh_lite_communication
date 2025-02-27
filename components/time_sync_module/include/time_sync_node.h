#include "time_sync_module.h"

#ifdef __cplusplus
extern "C" {
#endif

/* all functions, structs etc. */


esp_err_t add_time_sync_node_action_callbacks();

cJSON * handle_first_sync_time(cJSON * payload, uint32_t seq);
cJSON * handle_root_sync_time(cJSON * payload, uint32_t seq);
cJSON * handle_root_corrected_time(cJSON * payload, uint32_t seq);

#ifdef __cplusplus
}
#endif