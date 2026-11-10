#include "guard.h"

#include <Arduino.h>
#include <esp_ota_ops.h>
#include <esp_timer.h>
#include <stdlib.h>
#include <string.h>

// ODLOŽENÉ POTVRZENÍ FIRMWARU PO OTA (zpětný návrat bootloaderu na předchozí firmware)
//
// Arduino-ESP32 potvrdí novou aplikaci hned v initArduino(), tedy dřív, než se spustí setup(). Rollback
// v bootloaderu (v sestavení ze tvého v3 je podpora zapnutá: jádro odkazuje na esp_ota_*rollback*)
// tak nemá šanci nikdy zasáhnout. Tahle funkce potvrzení odloží: firmware se potvrdí až v guardConfirm(),
// tj. když se prokáže, že umí nastartovat web a 45 s běžet (nebo korektně skončil web relaci).
//   * Nový firmware padá / visí při startu -> první reset (pád, odpojení baterie) = bootloader nahraje
//     PŘEDCHOZÍ firmware (v3). Ani nouzový režim není potřeba.
//   * Bootloader bez podpory rollbacku: nic se nemění (guardConfirm jen vrátí chybu, kterou ignorujeme).
// extern "C": funkce v jádře je slabá C funkce; naše silná definice ji přebije.
extern "C" bool verifyRollbackLater() { return true; }

namespace {

// RTC_NOINIT: obsah přežije pád, watchdog i restart. Po zapnutí napájení je náhodný -> bootguard::valid()
// (magic + kontrolní součet) ho pozná a vynuluje. Typ nesmí mít inicializátory členů (viz bootguard.h).
RTC_NOINIT_ATTR bootguard::State g_state;

esp_timer_handle_t s_dead = nullptr;
volatile bool s_trial = false;  // viz guardTrial()

// Běží v tasku esp_timer, ne v zaseknutém tasku, takže zafunguje i při zacyklení hlavní smyčky.
// abort() = pád s restartem (ESP_RST_PANIC): při dalším startu ho bootguard započítá.
void deadCb(void*) { abort(); }

// Zapisuje hlavní smyčka (fáze, "healthy") i webový task (guardClear po OTA).
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

struct Lock {
  Lock() { portENTER_CRITICAL(&s_mux); }
  ~Lock() { portEXIT_CRITICAL(&s_mux); }
};

}  // namespace

bootguard::Decision guardBoot(bootguard::Cause cause, bool reedHeld) {
  // Který obraz (oddíl aplikace) právě běží: pády napočítané jiným obrazem se nepřenášejí (viz bootguard.h).
  const esp_partition_t* run = esp_ota_get_running_partition();
  const uint32_t part = run != nullptr ? run->address : 0;
  s_trial = guardUnconfirmed();
  Lock l;
  return bootguard::onBoot(g_state, cause, reedHeld, part);
}

void guardPhase(bootguard::Phase p) {
  Lock l;
  bootguard::setPhase(g_state, p);
}

void guardHealthy() {
  Lock l;
  bootguard::healthy(g_state);
}

void guardWebStable() {
  Lock l;
  bootguard::webStable(g_state);
}

void guardClear() {
  Lock l;
  bootguard::clear(g_state);
}

void guardAlive(uint32_t timeoutMs) {
  if (s_dead == nullptr) {
    esp_timer_create_args_t a;
    memset(&a, 0, sizeof(a));  // nezávislé na tom, jaké další členy struktura v dané verzi IDF má
    a.callback = &deadCb;
    a.dispatch_method = ESP_TIMER_TASK;
    a.name = "deadman";
    if (esp_timer_create(&a, &s_dead) != ESP_OK) {
      s_dead = nullptr;  // bez hlídání se dá žít, hlavní funkce zařízení to nepotřebuje
      return;
    }
  }
  (void)esp_timer_stop(s_dead);  // chyba "neběží" nevadí
  (void)esp_timer_start_once(s_dead, (uint64_t)timeoutMs * 1000ULL);
}

void guardConfirm() {
  // Rollback-API (esp_ota_mark_app_valid_cancel_rollback) potvrzuje obraz podle záznamu OTA, ne podle běžícího
  // oddílu. Po úspěšném Update.end() je v záznamu už NOVÝ obraz a restart přijde až za chvíli (knihovna OTA
  // restartuje po ~1 s): potvrzení by pak schválilo obraz, který nikdy neběžel. Proto jen když záznam ukazuje
  // na oddíl, ze kterého běžíme.
  const esp_partition_t* run = esp_ota_get_running_partition();
  const esp_partition_t* boot = esp_ota_get_boot_partition();
  if (run != nullptr && boot != nullptr && run->address != boot->address) return;
  (void)esp_ota_mark_app_valid_cancel_rollback();
  s_trial = guardUnconfirmed();
}

bool guardUnconfirmed() {
  const esp_partition_t* run = esp_ota_get_running_partition();
  if (run == nullptr) return false;
  esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
  if (esp_ota_get_state_partition(run, &st) != ESP_OK) return false;  // factory / bez OTA dat / bez podpory
  return st == ESP_OTA_IMG_PENDING_VERIFY;
}

bool guardTrial() { return s_trial; }

