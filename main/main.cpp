#include "Arduino.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_mesh_lite.h"
#include "esp_bridge.h"
#include <nvs_flash.h>
#include "WiFi.h"
#include <esp_wifi.h>

//#define IS_ROOT 1

void app_wifi_set_softap_info(void)
{
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
    return NULL;
}

esp_mesh_lite_msg_action_t action[] = {
    {"test", "test_ack", receive_test},
    {"test_ack", NULL, send_test_ack},
    {NULL, NULL, NULL}, //must be null terminated
};

static esp_err_t esp_storage_init(void)
{
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
    if(esp_mesh_lite_msg_action_list_register(action) != ESP_OK){
        xTimerStop(timer, 0);
        xTimerDelete(timer, 0);
    }
}

static void wifi_init(void)
{
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


// WIFI ERRORS: C:\_dev\esp\v5.3.2\esp-idf\components\esp_wifi\include\esp_wifi_types_generic.h
extern "C" void app_main()
{
    initArduino();
    pinMode(4, OUTPUT);
    digitalWrite(4, HIGH);
    // Do your own thing
    
    Serial.begin(115200);
    
    mesh_init();
    esp_mesh_lite_start();

    TimerHandle_t subscribe_to_msg_timer = xTimerCreate("subscribe_to_messages", 1000 / portTICK_PERIOD_MS, pdTRUE, NULL, subscribe_to_messages);
    TimerHandle_t send_test_message_timer = xTimerCreate("send_test_message", 1000 / portTICK_PERIOD_MS, pdTRUE, NULL, test_communcation);
    xTimerStart(subscribe_to_msg_timer, 5000);
    xTimerStart(send_test_message_timer, 6000);
     
    //esp_mesh_lite_connect();
}