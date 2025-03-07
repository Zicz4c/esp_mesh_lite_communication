#include "time_sync_root.h"

static const char *TAG = "time_sync_root";

static cJSON *node_data[MAX_MESH_NODES];
static delay_t node_delays[MAX_MESH_NODES];
static delay_t delays_rn[MAX_MESH_NODES];
static mac_addr_t nodes[MAX_MESH_NODES];
static size_t nodes_size = 0;

static int8_t num_of_known_child_nodes = 0;
static uint8_t num_of_responses = 0;
static uint32_t current_seq = 0;
// static const uint8_t resync_flag = 1;

static uint32_t sequences[MAX_CONCURRENT_SEQUENCES] = {0};
static uint32_t received_messages_for_sequence[MAX_CONCURRENT_SEQUENCES] = {0};
static cJSON *sequence_data[MAX_CONCURRENT_SEQUENCES];
TimerHandle_t resync_timer = NULL;
TimerHandle_t timeout_timer = NULL;

static bool timeout_occured = false;

// root side
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
static cJSON *queue_item = NULL;
cJSON *extra_delay = NULL;
void handle_resync(TimerHandle_t timer)
{
    cJSON *buffer;

    if (xQueueReceive(resync_queue, &buffer, 0))
    {
        //void update_num_of_known_child_nodes();

        ESP_LOGI(TAG, "[handle_resync] send data");

        ESP_LOGI(TAG, "[handle_resync] send data %s", cJSON_Print(queue_item));
        // send_first_sync_time();
        send_json_message(TIME_SYNC_ROOT_CORRECTED_TIME_MESSAGE, TIME_SYNC_ROOT_CORRECTED_TIME_MESSAGE_ACK, 0, queue_item, esp_mesh_lite_send_broadcast_msg_to_child);

        ESP_LOGI(TAG, "[handle_resync] delete data");
        cJSON *arr = cJSON_GetObjectItem(queue_item, JSON_NODE_DATA);
        cJSON_DeleteItemFromObject(queue_item, JSON_S);
        cJSON_DeleteItemFromObject(queue_item, JSON_US);
        ESP_LOGI(TAG, "[handle_resync] delete array");
        for (size_t i = 0; i < cJSON_GetArraySize(arr); i++)
        {
            cJSON_DeleteItemFromArray(arr, i);
        }
        for (size_t i = 0; i < cJSON_GetArraySize(extra_delay); i++)
        {
            cJSON_DeleteItemFromArray(extra_delay, i);
            /* code */
        }
        ESP_LOGI(TAG, "[handle_resync] reset queue");
        xQueueReset(resync_queue);
        queue_item = NULL;
        extra_delay = NULL;
    }
}

void handle_timeout(TimerHandle_t timer)
{
    xQueueReset(resync_queue);
    ESP_LOGW(TAG, "Timeout");
    timeout_occured = true;
    // xTimerStop(timeout_timer, 0);
    nodes_size = 0;
    for (size_t i = 0; i < MAX_CONCURRENT_SEQUENCES; i++)
    {
        sequences[i] = 0;
    }
    send_first_sync_time();
}

esp_err_t add_time_sync_root_action_callbacks()
{
    if (data_queue == NULL)
    {
        data_queue = xQueueCreate(100, sizeof(cJSON *));
    }
    if (resync_queue == NULL)
    {
        resync_queue = xQueueCreate(64, sizeof(uint8_t));
    }
    if (timeout_timer == NULL)
    {
        ESP_LOGI(TAG, "create timeout timer");
        timeout_timer = xTimerCreate("timeout", 2000 / portTICK_PERIOD_MS, pdFALSE, NULL, handle_timeout);
    }
    if (resync_timer == NULL)
    {
        resync_timer = xTimerCreate("resync", 1000 / portTICK_PERIOD_MS, pdTRUE, NULL, handle_resync);
    }
    xTimerStart(resync_timer, 0);
    return esp_mesh_lite_msg_action_list_register(time_sync_root_callbacks);
}

void count_responses(uint32_t *curren_seq, uint32_t recv_seq, uint8_t *num_of_responses)
{
    //  count responses, so we can controll the amount of broadcasts send
    if (*curren_seq == 0 || *curren_seq == recv_seq)
    {
        if (*curren_seq == recv_seq)
        {
            xTimerStop(timeout_timer, 0);
        }
        ESP_LOGI(TAG, "[count_responses] seq: %lu | recv_seq: %lu", *curren_seq, recv_seq);
        *curren_seq = recv_seq;
        (*num_of_responses)++;
        // ESP_LOGI(TAG, "[count_responses] num of responses: %d", *num_of_responses);
    }
}

uint32_t get_seq_from_json(cJSON *json)
{
    cJSON *msg_seq = cJSON_GetObjectItem(json, JSON_SEQ);
    return (uint32_t)cJSON_GetNumberValue(msg_seq);
}

void add_delays_to_data_array(cJSON **array, size_t num_of_nodes, delay_t *delays)
{
    struct timeval t_r;
    for (size_t i = 0; i < num_of_nodes; i++)
    {
        if (node_data[i] == NULL)
        {
            continue;
        }
        cJSON *data = node_data[i];
        delay_t d = delays[i];
        gettimeofday(&t_r, NULL);
        cJSON_AddNumberToObject(data, JSON_D_RN_S, d.s);
        cJSON_AddNumberToObject(data, JSON_D_RN_US, d.us);
        if (*array == NULL)
        {
            *array = cJSON_CreateArray();
        }
        cJSON_AddItemToArray(*array, data);
    }
}

void add_object_to_array(cJSON **array, cJSON *object)
{
    if (*array == NULL)
    {
        *array = cJSON_CreateArray();
    }
    cJSON_AddItemToArray(*array, object);
}

void update_num_of_known_child_nodes()
{

    uint32_t num_of_known_nodes = 0;

    esp_mesh_lite_get_nodes_list(&num_of_known_nodes);
    ESP_LOGI(TAG, "[update_num_of_known_child_nodes] num_of_known_nodes %lu", num_of_known_nodes);
    // first node is always the root itself
    num_of_known_child_nodes = num_of_known_nodes - 1;

    timeout_occured = false;
}

void handle_message_result(bool result, cJSON *data)
{
    if (result != ESP_OK)
    {
        // free memory when error occured
        cJSON_Delete(data);
    }
    else
    {
        // send to queue, so memory can be freed after ack
        if (data_queue != NULL)
        {
            xQueueSend(data_queue, &data, 0);
        }
    }
}

void handle_first_sequence(cJSON **data_array, uint32_t current_seq)
{
    if (current_seq == 0)
    {
        if (*data_array == NULL)
        {
            *data_array = cJSON_CreateArray();
        }
        // ESP_LOGI(TAG, "[handle_first_sequence] data array size: %d",cJSON_GetArraySize(*data_array));
        // ESP_LOGI(TAG, "[handle_first_sequence] %s", cJSON_Print(*data_array));
        for (size_t i = 0; i < cJSON_GetArraySize(*data_array); i++)
        {
            cJSON_DeleteItemFromArray(*data_array, i);
        }
    }
}

/// @brief searches for id of already known sequence, if sequence is not known a new id is set
/// @param seq
/// @return id of the sequence
int32_t get_sequence_id(uint32_t seq)
{
    int32_t sequence_id = -1;
    for (size_t i = 0; i < MAX_CONCURRENT_SEQUENCES && sequence_id < 0; i++)
    {
        if (sequences[i] == seq)
        {
            sequence_id = i;
        }
        else if (sequences[i] == 0)
        {
            sequence_id = i;
            sequences[i] = seq;
            sequence_data[i] = cJSON_CreateArray();
            ESP_LOGI(TAG, "[get_sequence_id] new sequence seq: %lu id:%ld", sequences[i], sequence_id);
        }
    }
    return sequence_id;
}

bool sequence_is_known(uint32_t seq)
{
    for (size_t i = 0; i < MAX_CONCURRENT_SEQUENCES; i++)
    {
        if (sequences[i] == seq)
        {
            return true;
        }
    }
    return false;
}

uint32_t get_num_of_received_messages_for_sequence(uint32_t seq)
{
    int32_t sequence_id = get_sequence_id(seq);
    if (sequence_id < 0)
    {
        return 0;
    }
    ESP_LOGI(TAG, "[get_num_of_received_messages_for_sequence] num: %lu", received_messages_for_sequence[(uint32_t)sequence_id]);
    received_messages_for_sequence[(uint32_t)sequence_id]++;
    ESP_LOGI(TAG, "[get_num_of_received_messages_for_sequence] num: %lu", received_messages_for_sequence[(uint32_t)sequence_id]);
    return received_messages_for_sequence[(uint32_t)sequence_id];
}

void clear_data_for_sequence(uint32_t seq)
{
    int32_t sequence_id = get_sequence_id(seq);
    uint32_t seq_id = (uint32_t)sequence_id;
    if (sequence_id < 0)
    {
        return;
    }
    ESP_LOGW(TAG, "[clear_data_for_sequence] seq_id: %lu", seq_id);
    sequences[seq_id] = 0;
    received_messages_for_sequence[seq_id] = 0;
    for (size_t i = 0; i < cJSON_GetArraySize(sequence_data[seq_id]); i++)
    {
        cJSON_DeleteItemFromArray(sequence_data[seq_id], i);
    }
    cJSON_Delete(sequence_data[seq_id]);
    sequence_data[seq_id] = NULL;
}

cJSON *add_time_to_json(cJSON *target)
{
    if (target == NULL)
    {
        target = cJSON_CreateObject();
    }

    struct timeval t_r;
    // get current time of root and add it to message
    gettimeofday(&t_r, NULL);
    cJSON_AddNumberToObject(target, JSON_S, t_r.tv_sec);
    cJSON_AddNumberToObject(target, JSON_US, t_r.tv_usec);

    return target;
}

esp_err_t send_first_sync_time()
{
    struct timeval t_r;

    update_num_of_known_child_nodes();
    ESP_LOGI(TAG, "[send_first_sync_time] num of known nodes: %d", num_of_known_child_nodes);
    if (num_of_known_child_nodes <= 0)
    {
        return ESP_FAIL;
    }

    // create json message with the current time
    cJSON *data = cJSON_CreateObject();
    add_time_to_json(data);

    // ESP_LOGI(TAG, "[send_first_sync_time] Sending first sync message %s", cJSON_Print(data));
    esp_err_t result = send_json_message(TIME_SYNC_FIRST_MESSAGE, TIME_SYNC_FIRST_MESSAGE_ACK, 0, data, esp_mesh_lite_send_broadcast_msg_to_child);
    handle_message_result(result, data);
    return result;
}

static cJSON *i_have_to_rename1 = NULL;
static uint8_t first_sync_num_of_responses = 0;
static uint32_t first_sync_current_seq = 0;
cJSON *handle_node_first_sync_time(cJSON *payload, uint32_t seq)
{
    struct timeval t_r;
    gettimeofday(&t_r, NULL);
    int us;
    int s;
    ESP_LOGI(TAG, "[---------BEGIN-handle_node_first_sync_time---------------------------]");
    ESP_LOGI(TAG, "[handle_node_first_sync_time] Received json: %s", cJSON_Print(payload));
    uint32_t recv_seq = get_seq_from_json(payload);
    int32_t seq_id = get_sequence_id(recv_seq);
    uint32_t num_of_received_messages = get_num_of_received_messages_for_sequence(recv_seq);
    ESP_LOGI(TAG, "[handle_node_first_sync_time] seq: %lu | recv_seq: %lu", seq, recv_seq);
    ESP_LOGI(TAG, "[handle_node_first_sync_time] seq_id: %ld", seq_id);
    ESP_LOGI(TAG, "[handle_node_first_sync_time] num_of_received_messages: %lu", num_of_received_messages);

    // handle_first_sequence(&i_have_to_rename1, first_sync_current_seq);

    //  count responses, to control the amount of broadcasts send
    // ESP_LOGI(TAG, "[handle_node_first_sync_time] stop 2");
    // count_responses(&first_sync_current_seq, recv_seq, &first_sync_num_of_responses);

    // ESP_LOGI(TAG, "[handle_node_first_sync_time] json: %s", cJSON_Print(i_have_to_rename1));

    // ESP_LOGI(TAG, "[handle_node_first_sync_time] stop 3");
    //  get values from json
    get_time_from_json(payload, &s, &us);
    // ESP_LOGI(TAG, "[handle_node_first_sync_time] stop 4");
    mac_addr_t mac = read_mac_from_json(payload);
    ESP_LOGI(TAG, "[handle_node_first_sync_time] Received mac:" MACSTR, MAC2STR(mac.addr));
    // search index of node
    // int id_index = index_of_node(nodes, &nodes_size, mac);
    // ESP_LOGI(TAG, "[handle_node_first_sync_time] stop 5");

    struct delay_t d;
    d.us = t_r.tv_usec - us;
    d.s = t_r.tv_sec - s;

    // node_delays[id_index] = d;

    cJSON *time_to_sync = cJSON_CreateObject();

    cJSON *data_mac = add_mac_to_json(time_to_sync, mac);
    // collect all responses before sending new time
    ESP_LOGI(TAG, "[handle_node_first_sync_time] num of responses: %ld/%d", num_of_received_messages, num_of_known_child_nodes);
    // node_data[id_index] = time_to_sync;
    //  ESP_LOGI(TAG, "[handle_node_first_sync_time] add to data %s:", cJSON_Print(node_data[id_index]));

    // add_object_to_array(&i_have_to_rename1, time_to_sync);
    add_object_to_array(&sequence_data[seq_id], time_to_sync);
    // cJSON_AddItemToArray(data_array, time_to_sync);
    if (num_of_received_messages >= num_of_known_child_nodes)
    {
        xTimerStop(timeout_timer, 0);
        cJSON *time_w_delay_data = cJSON_CreateObject();
        cJSON_AddItemToObject(time_w_delay_data, JSON_NODE_DATA, sequence_data[seq_id]);

        ESP_LOGI(TAG, "[handle_node_first_sync_time] all responses received");

        gettimeofday(&t_r, NULL);
        cJSON_AddNumberToObject(time_w_delay_data, JSON_S, t_r.tv_sec);
        cJSON_AddNumberToObject(time_w_delay_data, JSON_US, t_r.tv_usec);

        // ESP_LOGI(TAG, "[handle_node_first_sync_time] data_array: %s", cJSON_Print(data_array));
        ESP_LOGI(TAG, "[handle_node_first_sync_time] json: %s", cJSON_Print(time_w_delay_data));
        send_json_message(TIME_SYNC_ROOT_TIME_MESSAGE, TIME_SYNC_ROOT_TIME_MESSAGE, 0, time_w_delay_data, esp_mesh_lite_send_broadcast_msg_to_child);
        first_sync_current_seq = 0;
        first_sync_num_of_responses = 0;
        cJSON *arr = cJSON_GetObjectItem(time_w_delay_data, JSON_NODE_DATA);
        cJSON_DeleteItemFromObject(time_w_delay_data, JSON_S);
        cJSON_DeleteItemFromObject(time_w_delay_data, JSON_US);
        for (size_t i = 0; i < cJSON_GetArraySize(arr); i++)
        {
            cJSON_DeleteItemFromArray(arr, i);
        }
        clear_data_for_sequence(recv_seq);
    }
    else
    {
        ESP_LOGW(TAG, "[handle_node_first_sync_time] start timeout timer");
        xTimerStart(timeout_timer, 0 / portTICK_PERIOD_MS);
    }
    ESP_LOGI(TAG, "[------------------------------------END-------------------------------]");
    return NULL;
}

cJSON *to_node_data = NULL;
static uint8_t delay_sync_num_of_responses = 0;
static uint32_t delay_sync_current_seq = 0;
struct timeval time_delay_send;
static int32_t num_of_expected_packets = 0;
cJSON *handle_node_sync_time_w_delay(cJSON *payload, uint32_t seq)
{
    struct timeval t_r;
    gettimeofday(&t_r, NULL);

    ESP_LOGI(TAG, "[----------BEGIN-handle_node_sync_time_w_delay----------------]");

    // get seq from json => seq should be the sequence of TIME_SYNC_ROOT_TIME_MESSAGE
    cJSON *msg_seq = cJSON_GetObjectItem(payload, JSON_SEQ);
    uint32_t recv_seq = (uint32_t)cJSON_GetNumberValue(msg_seq);

    if (sequence_is_known(recv_seq))
    {
        delay_t zwischen_packeten;
        zwischen_packeten.s = t_r.tv_sec - time_delay_send.tv_sec;
        zwischen_packeten.us = t_r.tv_usec - time_delay_send.tv_usec;
        ESP_LOGW(TAG, "zwischen packeten %ld.%06ld", zwischen_packeten.s, zwischen_packeten.us);
    }
    else
    {
        if (to_node_data == NULL)
        {
            to_node_data = cJSON_CreateArray();
        }
        else
        {
            for (size_t i = 0; i < cJSON_GetArraySize(to_node_data); i++)
            {
                cJSON_DeleteItemFromArray(to_node_data, i);
            }
        }
    }

    //  count responses, so we can controll the amount of broadcasts send
    delay_sync_num_of_responses = get_num_of_received_messages_for_sequence(recv_seq);

    if (recv_seq == delay_sync_current_seq)
    {
        num_of_expected_packets--;
    }
    ESP_LOGI(TAG, "[handle_node_sync_time_w_delay] recv_seq: %lu", recv_seq);

    // count_responses(&delay_sync_current_seq, recv_seq, &delay_sync_num_of_responses);

    // parse data
    cJSON *node_id = cJSON_GetObjectItem(payload, JSON_MAC);
    // ESP_LOGI(TAG, "[handle_node_sync_time_w_delay] Received json: %s", cJSON_Print(payload));
    int32_t us = 0;
    int64_t s = 0;
    get_time_from_json(payload, &s, &us);
    delay_t d_rn;
    get_delay_from_json(payload, &d_rn);
    mac_addr_t mac = read_mac_from_json(node_id);

    int id_index = index_of_node(nodes, &nodes_size, mac);
    // ESP_LOGI(TAG, "handle_node_sync_time_w_delay - Time: %lld.%ld : %lu.%06lu", t_r.tv_sec, t_r.tv_usec, s, us);

    delay_t d_nr;
    d_nr.us = t_r.tv_usec - (us + d_rn.us);
    d_nr.s = t_r.tv_sec - (s + d_rn.s);
    int32_t d_nr_s_abs = abs(d_nr.s);
    handle_delay_second_overflow(&d_nr);

    // ESP_LOGI(TAG, "Delay_nr: %ld.%06ld",d_nr.s,d_nr.us);
    // ESP_LOGI(TAG, "Delay_rn: %ld.%06ld",d_rn.s,d_rn.us);
    // ESP_LOGI(TAG, "Delay?: %ld.%06ld - %ld.%06ld",d_rn.s + d_nr.s ,d_rn.us + d_nr.us, node_delays[id_index].s, node_delays[id_index].us);

    node_delays[id_index] = d_nr;
    cJSON *time_to_sync = cJSON_CreateObject();
    cJSON_AddNumberToObject(time_to_sync, JSON_D_S, d_nr.s);
    cJSON_AddNumberToObject(time_to_sync, JSON_D_US, d_nr.us);
    add_mac_to_json(time_to_sync, mac);
    node_data[id_index] = time_to_sync;
    add_object_to_array(&to_node_data, time_to_sync);
    ESP_LOGI(TAG, "[handle_node_sync_time_w_delay] add to array %s ", cJSON_Print(to_node_data));
    // ESP_LOGI(TAG, "num of responses: %d/%d", delay_sync_num_of_responses, num_of_known_child_nodes);
    if (delay_sync_num_of_responses >= num_of_known_child_nodes)
    {
        cJSON *root_corrected_data = cJSON_CreateObject();
        add_time_to_json(root_corrected_data);
        cJSON_AddItemToObject(root_corrected_data, JSON_NODE_DATA, to_node_data);

        ESP_LOGI(TAG, "[handle_node_sync_time_w_delay] json: %s", cJSON_Print(root_corrected_data));
        send_json_message(TIME_SYNC_ROOT_CORRECTED_TIME_MESSAGE, TIME_SYNC_ROOT_CORRECTED_TIME_MESSAGE_ACK,
                          0, root_corrected_data, esp_mesh_lite_send_broadcast_msg_to_child);
        xTimerStop(timeout_timer, 0);
        delay_sync_current_seq = 0;
        delay_sync_num_of_responses = 0;
        num_of_expected_packets = num_of_known_child_nodes;
        // cJSON_Delete(root_corrected_data);
        // for (size_t i = 0; i < cJSON_GetArraySize(delay_array); i++)
        //{
        //    //cJSON_Delete(node_data[i]);
        //    cJSON_DeleteItemFromArray(delay_array, i);
        //    node_data[i] = NULL;
        //}
        xQueueSend(data_queue, &root_corrected_data, 0);
    }
    else
    {
        // ESP_LOGW(TAG, "[handle_node_sync_time_w_delay] start timeout timer");

        xTimerStart(timeout_timer, 0 / portTICK_PERIOD_MS);
    }

    ESP_LOGI(TAG, "[----------END-handle_node_sync_time_w_delay----------------]");
    gettimeofday(&time_delay_send, NULL);
    return NULL;
}

mac_addr_t nodes_with_to_much_delay[MAX_MESH_NODES];
size_t num_of_nodes_with_to_much_delay = 0;
static uint8_t node_sync_num_of_responses = 0;
static uint32_t node_sync_current_seq = 0;
struct timeval time_checked;

cJSON *handle_node_sync_time(cJSON *payload, uint32_t seq)
{
    struct timeval t_r;

    delay_t zwischen_packeten;
    gettimeofday(&t_r, NULL);
    
    ESP_LOGI(TAG, "[----------BEGIN-handle_node_sync_time----------------]");
    cJSON *msg_seq = cJSON_GetObjectItem(payload, JSON_SEQ);
    uint32_t recv_seq = (uint32_t)cJSON_GetNumberValue(msg_seq);

    if (sequence_is_known(recv_seq))
    {
        ESP_LOGI(TAG, "[handle_node_sync_time] current_sequence %lu", node_sync_current_seq);

        zwischen_packeten.s = t_r.tv_sec - time_checked.tv_sec;
        zwischen_packeten.us = 10 * 1000 + t_r.tv_usec - time_checked.tv_usec;
        ESP_LOGW(TAG, "[handle_node_sync_time] zwischen packeten %ld.%06ld", zwischen_packeten.s, zwischen_packeten.us);
    }
    else
    {
        gettimeofday(&time_checked, NULL);
        zwischen_packeten.s = 0;
        zwischen_packeten.us = 10 * 1000;
    }
    // ESP_LOGI(TAG, "[handle_node_sync_time] Received json at: %lld:%ld", t_r.tv_sec, t_r.tv_usec);

    if (recv_seq == node_sync_current_seq)
    {
        num_of_expected_packets--;
    }
    ESP_LOGI(TAG, "[handle_node_sync_time] recv_seq: %lu", recv_seq);
    //  count responses, so we can controll the amount of broadcasts send
    node_sync_num_of_responses = get_num_of_received_messages_for_sequence(recv_seq);

    //count_responses(&node_sync_current_seq, recv_seq, &node_sync_num_of_responses);

    cJSON *recv_us = cJSON_GetObjectItem(payload, JSON_US);
    cJSON *recv_s = cJSON_GetObjectItem(payload, JSON_S);
    cJSON *node_id = cJSON_GetObjectItem(payload, JSON_MAC);
    int us = (int)cJSON_GetNumberValue(recv_us);
    int s = (int)cJSON_GetNumberValue(recv_s);

    us += zwischen_packeten.us;
    s += zwischen_packeten.s;

    mac_addr_t mac;
    for (size_t i = 0; i < 6; i++)
    {
        mac.addr[i] = (uint8_t)cJSON_GetNumberValue(cJSON_GetArrayItem(node_id, i));
    }

    int id_index = index_of_node(nodes, &nodes_size, mac);
    ESP_LOGI(TAG, "[handle_node_sync_time] INDEX %d", id_index);

    // ESP_LOGI(TAG, "[handle_node_sync_time] - Time: %lld.%06ld : %d.%06d", t_r.tv_sec, t_r.tv_usec, s, us);
    delay_t d_nr = node_delays[id_index];
    // delay_t d;
    // d.s = t_r.tv_sec - s;
    // d.us = t_r.tv_usec - us;
    // ESP_LOGI(TAG, "Delay N-R : %ld.%06ld", d_nr.s, d_nr.us);
    delay_t d_rest;
    d_rest.us = (t_r.tv_usec) - (us);
    d_rest.s = (t_r.tv_sec) - (s);
    int32_t d_rest_s_abs = abs(d_rest.s);
    handle_delay_second_overflow(&d_rest);

    // if (d_rest_s_abs == 1)
    //{
    //     int64_t d_rest_us = d_rest_s_abs * 1000000 + (d_rest.s > 0 ? d_rest.us : -d_rest.us);
    //     if (d_rest_us < 1000000 && d_rest_us > 0)
    //     {
    //         d_rest.us = d_rest_us;
    //         d_rest.s = 0;
    //     }
    // }
    ESP_LOGI(TAG, "Delay rest: %ld.%06ld", d_rest.s, d_rest.us);

    if (abs(d_rest.s) > 0 || (abs(d_rest.s) < 1 && abs(d_rest.us) > 9500))
    {
        // ESP_LOGI(TAG, "Delay to much: %ld.%06ld - " MACSTR, d_rest.s, d_rest.us, MAC2STR(mac.addr));
        if (extra_delay == NULL)
        {
            extra_delay = cJSON_CreateArray();
        }
        nodes_with_to_much_delay[num_of_nodes_with_to_much_delay] = mac;
        cJSON *delay_object = cJSON_CreateObject();
        add_mac_to_json(delay_object, mac);
        cJSON_AddNumberToObject(delay_object, JSON_D_S, d_rest.s);
        cJSON_AddNumberToObject(delay_object, JSON_D_US, d_rest.us);
        cJSON_AddItemToArray(extra_delay, delay_object);
        num_of_nodes_with_to_much_delay++;
    }
    ESP_LOGI(TAG, "[handle_node_sync_time] num of responses: %d/%d", node_sync_num_of_responses, num_of_known_child_nodes);

    if (node_sync_num_of_responses >= num_of_known_child_nodes && num_of_nodes_with_to_much_delay > 0)
    {
        ESP_LOGW(TAG, "[handle_node_sync_time] To much delay (%ld.%06ld) -> resync", d_rest.s, d_rest.us);

        // xQueueSend(resync_queue, &resync_flag, 0);
        xTimerStop(timeout_timer, 0);
        cJSON *time_w_delay_data = cJSON_CreateObject();
        add_time_to_json(time_w_delay_data);
        cJSON_AddItemToObject(time_w_delay_data, JSON_NODE_DATA, extra_delay);
        queue_item = time_w_delay_data;
        xQueueSend(resync_queue, queue_item, 0);
        num_of_expected_packets = num_of_nodes_with_to_much_delay;
        // send_json_message(TIME_SYNC_ROOT_TIME_MESSAGE, TIME_SYNC_ROOT_TIME_MESSAGE, 0, time_w_delay_data, esp_mesh_lite_send_broadcast_msg_to_child);
        clear_data_for_sequence(recv_seq);
        // cJSON_DeleteItemFromObject(time_w_delay_data, JSON_NODE_DATA);
        //  send_first_sync_time();
    }
    else
    {
        ESP_LOGW(TAG, "handle_node_sync_time: start timeout timer");

        // xTimerStart(timeout_timer, 0 / portTICK_PERIOD_MS);
    }

    // all answers received -> reset
    if (node_sync_num_of_responses >= num_of_known_child_nodes)
    {
        node_sync_current_seq = 0;
        node_sync_num_of_responses = 0;
        num_of_nodes_with_to_much_delay = 0;
    }
    ESP_LOGI(TAG, "[----------END-handle_node_sync_time----------------]");

    return NULL;
}

cJSON *handle_time_sync_request(cJSON *payload, uint32_t seq)
{
    ESP_LOGI(TAG, "Received time sync request");
    send_first_sync_time();
    return NULL;
}
