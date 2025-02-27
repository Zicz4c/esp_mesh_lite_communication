#include "espnow_comm_module.h"

static const char *TAG = "espnow_comm_module";

esp_err_t register_espnow_message_receive_callback(uint8_t message_type, void (*callback)(const uint8_t *mac_addr, const uint8_t *data, int len)){ 
    return esp_mesh_lite_espnow_recv_cb_register(message_type, callback);
}


void espnow_send_message(uint8_t message_type, uint8_t dst_mac[ESP_NOW_ETH_ALEN], const uint8_t *payload, uint8_t payload_len)
{
    if (esp_now_is_peer_exist(dst_mac) == false) {
        esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));
        if (peer == NULL) {
            ESP_LOGE(TAG, "Malloc peer information fail");
            return;
        }
        memset(peer, 0, sizeof(esp_now_peer_info_t));
        peer->channel = 0;
        peer->ifidx = ESP_IF_WIFI_STA;
        peer->encrypt = false;
        // memcpy(peer->lmk, CONFIG_ESPNOW_LMK, ESP_NOW_KEY_LEN);
        memcpy(peer->peer_addr, dst_mac, ESP_NOW_ETH_ALEN);
        esp_now_add_peer(peer);
        free(peer);
    }

    if (esp_mesh_lite_espnow_send(message_type, dst_mac, payload, payload_len) != ESP_OK) {
        ESP_LOGE(TAG, "Send error");
    }
    return;
}