#include "time_sync_module.h"

cJSON * handle_first_sync_time(cJSON * payload, uint32_t seq);
cJSON * handle_root_sync_time(cJSON * payload, uint32_t seq);
cJSON * handle_root_corrected_time(cJSON * payload, uint32_t seq);
