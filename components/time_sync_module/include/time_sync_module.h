#pragma once
#include "mac_helper.h"
#include "cJSON.h"
#include "meshlite_comm_module.h"
#include <sys/time.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_mesh_lite.h"
#include "freertos/queue.h"

#define MAX_MESH_NODES 50
#define TIME_SYNC_FIRST_MESSAGE "first_time"
#define TIME_SYNC_FIRST_MESSAGE_ACK "first_time_ack"
#define TIME_SYNC_ROOT_TIME_MESSAGE "root_time"
#define TIME_SYNC_ROOT_TIME_MESSAGE_ACK "root_time_ack"
#define TIME_SYNC_ROOT_CORRECTED_TIME_MESSAGE "corrected_time"
#define TIME_SYNC_ROOT_CORRECTED_TIME_MESSAGE_ACK "corrected_time_ack"
#define TIME_SYNC_NODE_FIRST_MESSAGE "node_time"
#define TIME_SYNC_NODE_FIRST_MESSAGE "node_time"
#define TIME_SYNC_NODE_FIRST_MESSAGE_ACK "node_time_ack"
#define TIME_SYNC_WITH_DELAY_MESSAGE "node_time_w_delay"
#define TIME_SYNC_WITH_DELAY_MESSAGE_ACK "node_time_w_delay_ack"
#define TIME_SYNC_MESSAGE "node_sync_time"
#define TIME_SYNC_MESSAGE_ACK "node_sync_time_ack"
#define TIME_SYNC_REQUEST "request_time_sync"
#define TIME_SYNC_REQUEST_ACK "request_time_sync_ack"
#define JSON_S "s"
#define JSON_US "us"
#define JSON_D_S "d_s"
#define JSON_D_RN_S "d_rn_s"
#define JSON_D_US "d_us"
#define JSON_D_RN_US "d_rn_us"
#define JSON_MAC "mac"
#define JSON_SEQ "seq"

#ifdef __cplusplus
extern "C" {
#endif
static QueueHandle_t data_queue = NULL;

typedef struct delay_t {
    int32_t s;
    int32_t us;
} delay_t;


cJSON * handle_ack(cJSON * payload, uint32_t seq);

int index_of_node(mac_addr_t * nodes, size_t nodes_size, mac_addr_t node_id);


#ifdef __cplusplus
}
#endif