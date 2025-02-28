#include "Arduino.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_mesh_lite.h"
#include "esp_bridge.h"
#include <nvs_flash.h>
#include "WiFi.h"
#include <esp_wifi.h>
#include "FreeRTOS/Queue.h"

#include "time_sync_root.h"
#include "time_sync_node.h"

#define IS_ROOT 1

#define define_edge(x)                 \
    static bool m_##x;                 \
    const bool x##_edge = x && !m_##x; \
    m_##x = x;


// Queue for memeory management
QueueHandle_t send_data_queue;
static size_t queue_size = 0;

void app_wifi_set_softap_info(void){
    char softap_ssid[32];
    uint8_t softap_mac[6];
    esp_wifi_get_mac(WIFI_IF_AP, softap_mac);
    memset(softap_ssid, 0x0, sizeof(softap_ssid));

    snprintf(softap_ssid, sizeof(softap_ssid), "%.32s", "myssid");
    esp_mesh_lite_set_softap_ssid_to_nvs(softap_ssid);
    esp_mesh_lite_set_softap_psw_to_nvs("mypassword");
    esp_mesh_lite_set_softap_info(softap_ssid, "mypassword");
}

/// @brief function for configuring the root node. 
/// should be called instead of esp_mesh_lite_init and after initial config is created
/// @param config 
void init_esp_mesh_lite_root(esp_mesh_lite_config_t * config){
    config->join_mesh_without_configured_wifi = false;
    esp_mesh_lite_init(config);
    app_wifi_set_softap_info();
    esp_mesh_lite_set_allowed_level(1);

}

/// @brief function for configuring the root node. 
/// should be called instead of esp_mesh_lite_init and after initial config is created
/// @param config 
void init_esp_mesh_lite_node(esp_mesh_lite_config_t * config){
    config->join_mesh_without_configured_wifi = true;
    esp_mesh_lite_init(config);
    app_wifi_set_softap_info();
    esp_mesh_lite_set_disallowed_level(1);
}

void test_communcation(TimerHandle_t timer){
    // send a message to all nodes
    cJSON * data = cJSON_CreateObject();
    
    cJSON_AddStringToObject(data, "test", "test");
#if IS_ROOT
    esp_mesh_lite_try_sending_msg("test", "test_ack", 2, data, esp_mesh_lite_send_broadcast_msg_to_child);
#else
    esp_mesh_lite_try_sending_msg("test", "test_ack", 2, data, esp_mesh_lite_send_msg_to_root);
#endif
    xQueueSend(send_data_queue, &data, 0);
    queue_size++;
}

cJSON * receive_test(cJSON * payload, uint32_t seq){
#if !IS_ROOT
    esp_mesh_lite_try_sending_msg("test", "test_ack", 0, payload, esp_mesh_lite_send_broadcast_msg_to_child);
#endif
    // do something with the received message
    cJSON * receviced_data = cJSON_GetObjectItem(payload, "test");
    Serial.printf("[main.c->receive_test] receved packet %lu with text:%s\n",seq, cJSON_GetStringValue(receviced_data));
    return NULL;
}

cJSON * send_test_ack(cJSON * paylaod, uint32_t seq){
    Serial.printf("[main.c->send_test_ack] receive ack for packet %lu \n", seq );
    cJSON* data;
    if(xQueuePeek(send_data_queue, &data,0)){
        cJSON * test = cJSON_GetObjectItem(data, "test");
        if(strcmp(cJSON_GetStringValue(test), "test") == 0){
            xQueueReceive(send_data_queue, &data, 0);
            cJSON_Delete(data);
            queue_size--;
        }
    }
    
    if(queue_size >= 100){
        while(xQueueReceive(send_data_queue, &data, 0)){
            cJSON_Delete(data);
        }
        xQueueReset(send_data_queue);
        queue_size = 0;
    }
    
    return NULL;
}

esp_mesh_lite_msg_action_t action[] = {
    {"test", "test_ack", receive_test},
    {"test_ack", NULL, send_test_ack},
    {NULL, NULL, NULL}, //must be null terminated
};

static esp_err_t esp_storage_init(void){
    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    return ret;
}

void subscribe_to_messages(TimerHandle_t timer){
    if(esp_mesh_lite_msg_action_list_register(action)   != ESP_OK 
#if IS_ROOT
    && add_time_sync_root_action_callbacks()        != ESP_OK 
#else
    && add_time_sync_node_action_callbacks()        != ESP_OK
#endif
    ){
        xTimerStop(timer, 0);
        xTimerDelete(timer, 0);
    }
}

static void wifi_init(void){
    // Station
    wifi_config_t wifi_config;
    memset(&wifi_config, 0x0, sizeof(wifi_config_t));
    esp_bridge_wifi_set_config(WIFI_IF_STA, &wifi_config);

    // Softap
    snprintf((char *)wifi_config.ap.ssid, sizeof(wifi_config.ap.ssid), "%s", "myssid");
    strlcpy((char *)wifi_config.ap.password, "mypassword", sizeof(wifi_config.ap.password));
    wifi_config.ap.channel = 11;
    esp_bridge_wifi_set_config(WIFI_IF_AP, &wifi_config);
}

void mesh_init(){
    esp_storage_init();
    esp_mesh_lite_config_t config = ESP_MESH_LITE_DEFAULT_INIT();
    config.softap_ssid = "myssid";//"Airhit";
    config.softap_password = "mypassword";//"die_ideealisten";
    
    esp_netif_init();
    esp_event_loop_create_default();
    esp_bridge_create_all_netif();
    wifi_init();
#if IS_ROOT
    init_esp_mesh_lite_root(&config);
#else
    init_esp_mesh_lite_node(&config);
#endif    
}

#if IS_ROOT
void time_sync(TimerHandle_t timer)
{
    esp_err_t err = send_first_sync_time();
    if(err != ESP_OK){
        ESP_LOGE("main.c", "Failed to send first sync message, err: %d", err);
    }
}
#endif
// WIFI ERRORS: C:\_dev\esp\v5.3.2\esp-idf\components\esp_wifi\include\esp_wifi_types_generic.h
extern "C" void app_main()
{
    initArduino();
    pinMode(4, OUTPUT);
    digitalWrite(4, HIGH);
    pinMode(2, INPUT_PULLUP);
    Serial.begin(115200);
    
    mesh_init();
    esp_mesh_lite_start();

    send_data_queue = xQueueCreate(100, sizeof(cJSON *));

    TimerHandle_t subscribe_to_msg_timer = xTimerCreate("subscribe_to_messages", 1000 / portTICK_PERIOD_MS, pdTRUE, NULL, subscribe_to_messages);
    //TimerHandle_t send_test_message_timer = xTimerCreate("send_test_message", 1000 / portTICK_PERIOD_MS, pdTRUE, NULL, test_communcation);
    xTimerStart(subscribe_to_msg_timer, 5000);
    //xTimerStart(send_test_message_timer, 6000);
#if IS_ROOT
    TimerHandle_t time_sync_timer = xTimerCreate("time_sync", 20000 / portTICK_PERIOD_MS, true, NULL, &time_sync);
    xTimerStart(time_sync_timer, 7000);
#else
    //esp_mesh_lite_try_sending_msg(TIME_SYNC_REQUEST, TIME_SYNC_FIRST_MESSAGE_ACK, 0, NULL, esp_mesh_lite_send_msg_to_root);
#endif
struct timeval t;
//esp_mesh_lite_connect();
while (1)
{
    int pin_pressed = digitalRead(2);
    define_edge(pin_pressed);
    if (pin_pressed_edge)
    {
            gettimeofday(&t, NULL); 
            Serial.printf("Button pressed: %llu:%06lu\n", t.tv_sec, t.tv_usec);
            //esp_mesh_lite_try_sending_msg("test", "test_ack", 0, NULL, esp_mesh_lite_send_broadcast_msg_to_child);
        }
        vTaskDelay(1 / portTICK_PERIOD_MS);
    }
    
}