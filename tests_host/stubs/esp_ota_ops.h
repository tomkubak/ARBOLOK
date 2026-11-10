#pragma once
#include <stdint.h>

#include "esp_err.h"
typedef struct {
  uint32_t address;
  uint32_t size;
} esp_partition_t;
typedef enum {
  ESP_OTA_IMG_NEW = 0x0U,
  ESP_OTA_IMG_PENDING_VERIFY = 0x1U,
  ESP_OTA_IMG_VALID = 0x2U,
  ESP_OTA_IMG_INVALID = 0x3U,
  ESP_OTA_IMG_ABORTED = 0x4U,
  ESP_OTA_IMG_UNDEFINED = 0xFFFFFFFFU
} esp_ota_img_states_t;
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void);
const esp_partition_t* esp_ota_get_running_partition(void);
const esp_partition_t* esp_ota_get_boot_partition(void);
esp_err_t esp_ota_get_state_partition(const esp_partition_t* partition, esp_ota_img_states_t* ota_state);
