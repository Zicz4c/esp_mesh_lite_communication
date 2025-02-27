#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_log.h"
#define MAC_ADDR_SIZE 6

/// @brief mac address. Format: uint8_t[6]
typedef struct mac_addr_t { uint8_t addr[MAC_ADDR_SIZE]; } mac_addr_t;

bool equal_mac(mac_addr_t ls, mac_addr_t rs);