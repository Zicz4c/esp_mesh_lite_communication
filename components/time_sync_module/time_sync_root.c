#include "time_sync_root.h"

static const char *TAG = "time_sync_root";

static delay_t node_delays[MAX_MESH_NODES];
static mac_addr_t nodes[MAX_MESH_NODES];
static size_t nodes_size = 0;

static uint8_t num_of_known_child_nodes = 0;
static uint8_t num_of_responses = 0;
static uint32_t current_seq = 0;
static const uint8_t resync_flag = 1;

TimerHandle_t resync_timer = NULL;
TimerHandle_t timeout_timer = NULL;

static bool timeout_occured = false;

// root side
#pragma region root_side
esp_mesh_lite_msg_action_t time_sync_root_callbacks[] = {
    {TIME_SYNC_NODE_FIRST_MESSAGE, TIME_SYNC_NODE_FIRST_MESSAGE_ACK, handle_node_first_sync_time},
    {TIME_SYNC_NODE_FIRST_MESSAGE_ACK, NULL, handle_ack},
    {TIME_SYNC_WITH_DELAY_MESSAGE, TIME_SYNC_WITH_DELAY_MESSAGE_ACK, handle_node_sync_time_w_delay},
    {TIME_SYNC_WITH_DELAY_MESSAGE_ACK, NULL, handle_ack},
    {TIME_SYNC_MESSAGE, TIME_SYNC_MESSAGE_ACK, handle_node_sync_time},
    {TIME_SYNC_MESSAGE_ACK, NULL, handle_ack},
    {TIME_SYNC_REQUEST, TIME_SYNC_REQUEST_ACK, handle_time_sync_request},
    {TIME_SYNC_REQUEST_ACK, NULL, handle_ack},
    {NULL, NULL, NULL}};

QueueHandle_t resync_queue = NULL;

void handle_resync(TimerHandle_t timer)
{
    uint8_t buffer;

    if (xQueueReceive(resync_queue, &buffer, 0))
    {
        xQueueReset(resync_queue);
        send_first_sync_time();
    }
}

void handle_timeout(TimerHandle_t timer)
{
    xQueueReset(resync_queue);
    ESP_LOGW(TAG, "Timeout");
    timeout_occured = true;
    // xTimerStop(timeout_timer, 0);
    send_first_sync_time();
}

esp_err_t add_time_sync_root_action_callbacks()
{   
    if(data_queue == NULL){
        data_queue = xQueueCreate(100, sizeof(cJSON *));
    }
    if (resync_queue == NULL)
    {
        resync_queue = xQueueCreate(64, sizeof(uint8_t));
    }
    if (timeout_timer == NULL)
    {

        ESP_LOGI(TAG, "create timeout timer");
        timeout_timer = xTimerCreate("timeout", 1000 / portTICK_PERIOD_MS, pdFALSE, NULL, handle_timeout);
    }
    if (resync_timer == NULL)
    {
        resync_timer = xTimerCreate("resync", 1000 / portTICK_PERIOD_MS, pdTRUE, NULL, handle_resync);
    }
    xTimerStart(resync_timer, 0);
    return esp_mesh_lite_msg_action_list_register(time_sync_root_callbacks);
}

esp_err_t send_first_sync_time()
{
    struct timeval t_r;

    uint32_t num_of_known_nodes = 0;
    if (timeout_occured && num_of_known_child_nodes > 0)
    {
        num_of_known_child_nodes--;
    }
    else
    {
        esp_mesh_lite_get_nodes_list(&num_of_known_nodes);
        
        // first node is always the root itself
        num_of_known_child_nodes = num_of_known_nodes - 1;
    }
    timeout_occured = false;

    // create json message with the current time
    cJSON *data = cJSON_CreateObject();
    // get current time of root and add it to message
    gettimeofday(&t_r, NULL);
    cJSON_AddNumberToObject(data, JSON_S, t_r.tv_sec);
    cJSON_AddNumberToObject(data, JSON_US, t_r.tv_usec);

    ESP_LOGI(TAG, "Sending first sync message %s", cJSON_Print(data));
    esp_err_t result = send_json_message(TIME_SYNC_FIRST_MESSAGE, TIME_SYNC_FIRST_MESSAGE_ACK, 0, data, esp_mesh_lite_send_broadcast_msg_to_child);
    if(result != ESP_OK){
        cJSON_Delete(data);
        
    }else{
        xQueueSend(data_queue, &data, 0);
    }
    return result;
}

static cJSON *time_w_delay_data;
static uint8_t first_sync_num_of_responses = 0;
static uint32_t first_sync_current_seq = 0;
cJSON *handle_node_first_sync_time(cJSON *payload, uint32_t seq)
{
    struct timeval t_r;
    gettimeofday(&t_r, NULL);
    // ESP_LOGI(TAG, "seq: %lu", seq);
    cJSON *msg_seq = cJSON_GetObjectItem(payload, JSON_SEQ);
    uint32_t recv_seq = (uint32_t)cJSON_GetNumberValue(msg_seq);
    // ESP_LOGI(TAG, "recv_seq: %lu", recv_seq);
    //  count responses, so we can controll the amount of broadcasts send
    if (first_sync_current_seq == 0 || first_sync_current_seq == recv_seq)
    {
        if (first_sync_current_seq == 0)
        {
            time_w_delay_data = cJSON_CreateArray();
        }
        first_sync_current_seq = recv_seq;
        first_sync_num_of_responses++;
        // ESP_LOGI(TAG, "first sync num of responses: %d", first_sync_num_of_responses);
    }

    // read json
    cJSON *recv_us = cJSON_GetObjectItem(payload, JSON_US);
    cJSON *recv_s = cJSON_GetObjectItem(payload, JSON_S);
    cJSON *node_id = cJSON_GetObjectItem(payload, JSON_MAC);

    // get values from json
    int us = (int)cJSON_GetNumberValue(recv_us);
    int s = (int)cJSON_GetNumberValue(recv_s);
    mac_addr_t mac;
    for (size_t i = 0; i < 6; i++)
    {
        mac.addr[i] = (uint8_t)cJSON_GetNumberValue(cJSON_GetArrayItem(node_id, i));
    }
    // search index of node
    int id_index = index_of_node(nodes, nodes_size, mac);
    if (id_index < 0)
    {
        // if node is not known add it to the list
        nodes[nodes_size] = mac;
        id_index = nodes_size;
        nodes_size++;
    }

    // get root time

    // ESP_LOGI(TAG, "Time: %lld.%ld : %d.%06d", t_r.tv_sec, t_r.tv_usec, s, us);
    //  calculate delay of whole transmission
    struct delay_t d;
    d.us = t_r.tv_usec - us;
    d.s = t_r.tv_sec - s;
    //int32_t d_s_abs = abs(d.s);
    //if (d_s_abs == 1)
    //{
    //    int64_t d_us = d_s_abs * 1000000 + (d.s > 0 ? d.us : -d.us);
    //    if (d_us < 1000000 && d_us > 0)
    //    {
    //        d.us = d_us;
    //        d.s = 0;
    //    }
    //}
    // ESP_LOGI(TAG, "Delay: %ld.%06ld", d.s, d.us);
    //  save delay of node
    node_delays[id_index] = d;

    cJSON *time_to_sync = cJSON_CreateObject();
    cJSON_AddNumberToObject(time_to_sync, JSON_S, t_r.tv_sec);
    cJSON_AddNumberToObject(time_to_sync, JSON_US, t_r.tv_usec);
    cJSON_AddNumberToObject(time_to_sync, JSON_D_US, d.us);
    cJSON_AddNumberToObject(time_to_sync, JSON_D_S, d.s);
    cJSON *data_mac = cJSON_AddArrayToObject(time_to_sync, JSON_MAC);
    for (size_t i = 0; i < MAC_ADDR_SIZE; i++)
    {
        cJSON *mac_part = cJSON_CreateNumber(mac.addr[i]);
        cJSON_AddItemToArray(data_mac, mac_part);
    }
    cJSON_AddItemToArray(time_w_delay_data, time_to_sync);
    // collect all responses before sending new time
    // ESP_LOGI(TAG, "num of responses: %d/%d", first_sync_num_of_responses, num_of_known_child_nodes);

    if (first_sync_num_of_responses >= num_of_known_child_nodes)
    {
        // xTimerStop(timeout_timer, 0);
        //  ESP_LOGI(TAG, "first json: %s", cJSON_Print(time_w_delay_data));
        send_json_message(TIME_SYNC_ROOT_TIME_MESSAGE, TIME_SYNC_ROOT_TIME_MESSAGE, 0, time_w_delay_data, esp_mesh_lite_send_broadcast_msg_to_child);
        first_sync_current_seq = 0;
        first_sync_num_of_responses = 0;
        for (size_t i = 0; i < cJSON_GetArraySize(time_w_delay_data); i++)
        {
            cJSON_DeleteItemFromArray(time_w_delay_data, i);
        }
    }
    else
    {
        ESP_LOGW(TAG, "start timeout timer");
        xTimerStart(timeout_timer, 1000 / portTICK_PERIOD_MS);
    }
    return NULL;
}

cJSON *root_corrected_data;
static uint8_t delay_sync_num_of_responses = 0;
static uint32_t delay_sync_current_seq = 0;
cJSON *handle_node_sync_time_w_delay(cJSON *payload, uint32_t seq)
{
    struct timeval t_r;
    gettimeofday(&t_r, NULL);

    if (delay_sync_current_seq == 0)
    {
        root_corrected_data = cJSON_CreateArray();
    }
    cJSON *msg_seq = cJSON_GetObjectItem(payload, JSON_SEQ);
    uint32_t recv_seq = (uint32_t)cJSON_GetNumberValue(msg_seq);
    // ESP_LOGI(TAG, "recv_seq: %lu", recv_seq);
    //  count responses, so we can controll the amount of broadcasts send
    if (delay_sync_current_seq == 0 || delay_sync_current_seq == recv_seq)
    {
        delay_sync_current_seq = recv_seq;
        delay_sync_num_of_responses++;
        // ESP_LOGI(TAG, "incr delay sync num of responses: %d", delay_sync_num_of_responses);
    }
    cJSON *recv_us = cJSON_GetObjectItem(payload, JSON_US);
    cJSON *recv_s = cJSON_GetObjectItem(payload, JSON_S);
    cJSON *recv_d_rn_us = cJSON_GetObjectItem(payload, JSON_D_RN_US);
    cJSON *recv_d_rn_s = cJSON_GetObjectItem(payload, JSON_D_RN_S);
    cJSON *node_id = cJSON_GetObjectItem(payload, JSON_MAC);
    // ESP_LOGI(TAG, "Received time with delay: %s", cJSON_Print(payload));
    uint32_t us = (uint32_t)cJSON_GetNumberValue(recv_us);
    uint32_t s = (uint32_t)cJSON_GetNumberValue(recv_s);
    delay_t d_rn;
    d_rn.us = (int)cJSON_GetNumberValue(recv_d_rn_us);
    d_rn.s = (int)cJSON_GetNumberValue(recv_d_rn_s);
    mac_addr_t mac;
    for (size_t i = 0; i < 6; i++)
    {
        mac.addr[i] = (uint8_t)cJSON_GetNumberValue(cJSON_GetArrayItem(node_id, i));
    }

    int id_index = index_of_node(nodes, nodes_size, mac);
    if (id_index < 0)
    {
        nodes[nodes_size] = mac;
        id_index = nodes_size;
        nodes_size++;
    }

    // ESP_LOGI(TAG, "handle_node_sync_time_w_delay - Time: %lld.%ld : %lu.%06lu", t_r.tv_sec, t_r.tv_usec, s, us);

    delay_t d_nr;
    d_nr.us = t_r.tv_usec - (us + d_rn.us);
    d_nr.s = t_r.tv_sec - (s + d_rn.s);
    //int32_t d_nr_s_abs = abs(d_nr.s);
    //if (d_nr_s_abs == 1)
    //{
    //    int64_t d_us = d_nr_s_abs * 1000000 + (d_nr.s > 0 ? d_nr.us : -d_nr.us);
    //    if (d_us < 1000000 && d_us > 0)
    //    {
    //        d_nr.us = d_us;
    //        d_nr.s = 0;
    //    }
    //}
    // ESP_LOGI(TAG, "Delay_nr: %ld.%06ld",d_nr.s,d_nr.us);
    // ESP_LOGI(TAG, "Delay_rn: %ld.%06ld",d_rn.s,d_rn.us);
    // ESP_LOGI(TAG, "Delay?: %ld.%06ld - %ld.%06ld",d_rn.s + d_nr.s ,d_rn.us + d_nr.us, node_delays[id_index].s, node_delays[id_index].us);

    node_delays[id_index] = d_nr;
    cJSON *time_to_sync = cJSON_CreateObject();
    gettimeofday(&t_r, NULL);
    cJSON_AddNumberToObject(time_to_sync, JSON_S, t_r.tv_sec + d_nr.s);
    cJSON_AddNumberToObject(time_to_sync, JSON_US, t_r.tv_usec + d_nr.us);
    cJSON *data_mac = cJSON_AddArrayToObject(time_to_sync, JSON_MAC);
    for (size_t i = 0; i < MAC_ADDR_SIZE; i++)
    {
        cJSON *mac_part = cJSON_CreateNumber(mac.addr[i]);
        cJSON_AddItemToArray(data_mac, mac_part);
    }
    cJSON_AddItemToArray(root_corrected_data, time_to_sync);
    // ESP_LOGI(TAG, "num of responses: %d/%d", delay_sync_num_of_responses, num_of_known_child_nodes);
    if (delay_sync_num_of_responses >= num_of_known_child_nodes)
    {
        // ESP_LOGI(TAG, "sync json: %s", cJSON_Print(root_corrected_data));

        send_json_message(TIME_SYNC_ROOT_CORRECTED_TIME_MESSAGE, TIME_SYNC_ROOT_CORRECTED_TIME_MESSAGE_ACK,
                          0, root_corrected_data, esp_mesh_lite_send_broadcast_msg_to_child);
        xTimerStop(timeout_timer, 0);
        delay_sync_current_seq = 0;
        delay_sync_num_of_responses = 0;
        // cJSON_Delete(root_corrected_data);
        for (size_t i = 0; i < cJSON_GetArraySize(root_corrected_data); i++)
        {
            cJSON_DeleteItemFromArray(root_corrected_data, i);
        }
    }
    else
    {
        ESP_LOGW(TAG, "handle_node_sync_time_w_delay: start timeout timer");

        xTimerStart(timeout_timer, 1000 / portTICK_PERIOD_MS);
    }
    return NULL;
}

mac_addr_t nodes_with_to_much_delay[MAX_MESH_NODES];
size_t num_of_nodes_with_to_much_delay = 0;
static uint8_t node_sync_num_of_responses = 0;
static uint32_t node_sync_current_seq = 0;
cJSON *handle_node_sync_time(cJSON *payload, uint32_t seq)
{
    struct timeval t_r;

    gettimeofday(&t_r, NULL);
    cJSON *msg_seq = cJSON_GetObjectItem(payload, JSON_SEQ);
    uint32_t recv_seq = (uint32_t)cJSON_GetNumberValue(msg_seq);
    // ESP_LOGI(TAG, "recv_seq: %lu", recv_seq);
    //  count responses, so we can controll the amount of broadcasts send
    if (node_sync_current_seq == 0 || node_sync_current_seq == recv_seq)
    {
        node_sync_current_seq = recv_seq;
        node_sync_num_of_responses++;
        // ESP_LOGI(TAG, "incr node sync num of responses: %d", node_sync_num_of_responses);
    }
    cJSON *recv_us = cJSON_GetObjectItem(payload, JSON_US);
    cJSON *recv_s = cJSON_GetObjectItem(payload, JSON_S);
    cJSON *node_id = cJSON_GetObjectItem(payload, JSON_MAC);
    int us = (int)cJSON_GetNumberValue(recv_us);
    int s = (int)cJSON_GetNumberValue(recv_s);

    mac_addr_t mac;
    for (size_t i = 0; i < 6; i++)
    {
        mac.addr[i] = (uint8_t)cJSON_GetNumberValue(cJSON_GetArrayItem(node_id, i));
    }

    int id_index = index_of_node(nodes, nodes_size, mac);
    if (id_index < 0)
    {
        nodes[nodes_size] = mac;
        id_index = nodes_size;
        nodes_size++;
    }

    ESP_LOGI(TAG, "handle_node_sync_time - Time: %lld.%06ld : %d.%06d", t_r.tv_sec, t_r.tv_usec, s, us);
    delay_t d_nr = node_delays[id_index];
    // delay_t d;
    // d.s = t_r.tv_sec - s;
    // d.us = t_r.tv_usec - us;
    // ESP_LOGI(TAG, "Delay: %ld.%06ld", d.s, d.us);
    delay_t d_rest;
    d_rest.us = (t_r.tv_usec) - (us);
    d_rest.s = (t_r.tv_sec) - (s);
    int32_t d_rest_s_abs = abs(d_rest.s);
    if (d_rest_s_abs == 1)
    {
        int64_t d_rest_us = d_rest_s_abs * 1000000 + (d_rest.s > 0 ? d_rest.us : -d_rest.us);
        if (d_rest_us < 1000000 && d_rest_us > 0)
        {
            d_rest.us = d_rest_us;
            d_rest.s = 0;
        }
    }
    // ESP_LOGI(TAG, "Delay rest: %ld.%06ld", d_rest.s, d_rest.us);

    if (d_rest.s > 0 || (d_rest.s < 1 && d_rest.us > 9500))
    {
        ESP_LOGI(TAG, "Delay to much: %ld.%06ld - " MACSTR, d_rest.s, d_rest.us, MAC2STR(mac.addr));
        nodes_with_to_much_delay[num_of_nodes_with_to_much_delay] = mac;
        num_of_nodes_with_to_much_delay++;
    }

    if (node_sync_num_of_responses >= num_of_known_child_nodes && num_of_nodes_with_to_much_delay > 0)
    {
        ESP_LOGW(TAG, "To much delay (%ld.%06ld) -> resync", d_rest.s, d_rest.us);

        xQueueSend(resync_queue, &resync_flag, 0);
        xTimerStop(timeout_timer, 0);
        num_of_nodes_with_to_much_delay = 0;
        node_sync_num_of_responses = 0;
        node_sync_current_seq = 0;
        // send_json_message(TIME_SYNC_ROOT_TIME_MESSAGE, TIME_SYNC_ROOT_TIME_MESSAGE_ACK,
        //                   0, time_to_sync, esp_mesh_lite_send_broadcast_msg_to_child);
        
        // send_first_sync_time();
    }
    else if (node_sync_num_of_responses < num_of_known_child_nodes)
    {
        ESP_LOGW(TAG, "handle_node_sync_time: start timeout timer");

        xTimerStart(timeout_timer, 1000 / portTICK_PERIOD_MS);
    }
    return NULL;
}

cJSON *handle_time_sync_request(cJSON *payload, uint32_t seq)
{
    ESP_LOGI(TAG, "Received time sync request");
    send_first_sync_time();
    return NULL;
}

#pragma endregion
