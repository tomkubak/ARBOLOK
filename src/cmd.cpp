#include "cmd.h"

#include <Arduino.h>

static QueueHandle_t s_q = nullptr;
static portMUX_TYPE s_locMux = portMUX_INITIALIZER_UNLOCKED;
static Location s_locStage[LOC_MAX];
static uint8_t s_locStageN = 0;  // 0 = nic nečeká

bool cmdStageLocs(const Location* ls, uint8_t n) {
  if (!ls || n < 1 || n > LOC_MAX) return false;
  bool ok = false;
  portENTER_CRITICAL(&s_locMux);
  if (s_locStageN == 0) {
    for (uint8_t i = 0; i < n; i++) s_locStage[i] = ls[i];
    s_locStageN = n;
    ok = true;
  }
  portEXIT_CRITICAL(&s_locMux);
  return ok;
}

bool cmdTakeLocs(Location* ls, uint8_t& n) {
  portENTER_CRITICAL(&s_locMux);
  n = s_locStageN;
  for (uint8_t i = 0; i < n; i++) ls[i] = s_locStage[i];
  s_locStageN = 0;
  portEXIT_CRITICAL(&s_locMux);
  return n > 0;
}

void cmdInit() {
  if (!s_q) s_q = xQueueCreate(6, sizeof(Cmd));
}

bool cmdPost(const Cmd& c) {
  if (!s_q) return false;
  return xQueueSend(s_q, &c, 0) == pdTRUE;
}

bool cmdTake(Cmd& c) {
  if (!s_q) return false;
  return xQueueReceive(s_q, &c, 0) == pdTRUE;
}
