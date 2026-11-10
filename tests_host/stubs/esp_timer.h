#pragma once
// Atrapa podle ESP-IDF 5.x (components/esp_timer/include/esp_timer.h): jen to, co firmware používá.
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct esp_timer* esp_timer_handle_t;
typedef void (*esp_timer_cb_t)(void* arg);
typedef enum { ESP_TIMER_TASK, ESP_TIMER_MAX } esp_timer_dispatch_t;
typedef struct {
  esp_timer_cb_t callback;
  void* arg;
  esp_timer_dispatch_t dispatch_method;
  const char* name;
  bool skip_unhandled_events;
} esp_timer_create_args_t;
esp_err_t esp_timer_create(const esp_timer_create_args_t* create_args, esp_timer_handle_t* out_handle);
esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t timeout_us);
esp_err_t esp_timer_stop(esp_timer_handle_t timer);
