#include "power.h"

#include <Arduino.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <esp_system.h>

#include "config.h"
#include "guard.h"
#include "probe.h"
#include "wiring.h"

namespace {

// Piny, které se v hlubokém spánku drží v LOW. K nim přibude pin BIAS sondy podle toho, který se právě používá
// (od v4.3 se pin pozná za běhu, viz wiring.h).
const gpio_num_t kHoldLow[] = {(gpio_num_t)LED_PIN, (gpio_num_t)VDIV_ON_PIN, (gpio_num_t)VREF_ON_PIN};

}  // namespace

BootKind powerBootKind() {
  switch (esp_sleep_get_wakeup_cause()) {
    case ESP_SLEEP_WAKEUP_TIMER: return BK_TIMER;
    case ESP_SLEEP_WAKEUP_EXT0: return BK_REED;
    default: break;
  }
  switch (esp_reset_reason()) {
    case ESP_RST_BROWNOUT: return BK_BROWNOUT;
    case ESP_RST_PANIC:
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return BK_CRASH;
    default: return BK_POWER;
  }
}

bootguard::Cause powerBootCause() {
  switch (esp_sleep_get_wakeup_cause()) {
    case ESP_SLEEP_WAKEUP_TIMER:
    case ESP_SLEEP_WAKEUP_EXT0: return bootguard::C_WAKE;
    default: break;
  }
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:
    case ESP_RST_EXT: return bootguard::C_POWER;
    case ESP_RST_SW: return bootguard::C_SW;
    case ESP_RST_DEEPSLEEP: return bootguard::C_WAKE;
    case ESP_RST_PANIC:
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return bootguard::C_CRASH;
    case ESP_RST_BROWNOUT: return bootguard::C_BROWNOUT;
    default: return bootguard::C_OTHER;
  }
}

bool powerReedHeld(uint32_t holdMs) {
  pinMode(WAKE_PIN, INPUT_PULLUP);
  delay(5);  // ustálení vnitřního pull-upu
  if (digitalRead(WAKE_PIN) != LOW) return false;  // běžný případ: bez magnetu se nečeká
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);  // magnet je vidět
  const uint32_t t0 = millis();
  bool held = true;
  while ((uint32_t)(millis() - t0) < holdMs) {
    if (digitalRead(WAKE_PIN) != LOW) {
      held = false;
      break;
    }
    delay(10);
  }
  digitalWrite(LED_PIN, LOW);
  return held;
}

void powerReleaseHolds() {
  for (size_t i = 0; i < sizeof(kHoldLow) / sizeof(kHoldLow[0]); i++) {
    rtc_gpio_hold_dis(kHoldLow[i]);
    rtc_gpio_deinit(kHoldLow[i]);
  }
  // Zadržený byl pin BIAS, který se používal před spánkem. Nastavení (uložený pin) se načítá až později, proto se
  // uvolní všichni kandidáti; uvolnění pinu, který držený nebyl, nic neudělá.
  for (size_t i = 0; i < wiring::kPinCount; i++) {
    rtc_gpio_hold_dis((gpio_num_t)wiring::kPins[i]);
    rtc_gpio_deinit((gpio_num_t)wiring::kPins[i]);
  }
  rtc_gpio_hold_dis((gpio_num_t)WAKE_PIN);
  rtc_gpio_deinit((gpio_num_t)WAKE_PIN);
}

void powerInitPins() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  pinMode(WAKE_PIN, INPUT_PULLUP);  // jazýčkové relé proti GND
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
}

void powerSerial(char* out, size_t cap) {
  const uint64_t chipid = ESP.getEfuseMac();
  snprintf(out, cap, "%07u", (unsigned)(chipid % 10000000ULL));
}

void powerBlink(uint8_t n, uint16_t onMs, uint16_t offMs) {
  for (uint8_t i = 0; i < n; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(onMs);
    digitalWrite(LED_PIN, LOW);
    if (i + 1 < n) delay(offMs);
  }
}

[[noreturn]] void powerSleep(uint32_t timerSec, bool& waitRelease) {
  // Vše, co by mohlo téct proud, vypnout (některé už jsou LOW, pro jistotu znovu).
  const uint8_t bias = probeBiasPin();
  probeRelease();  // BIAS dole, druhá elektroda (je-li na pinu) vysoká impedance: ve spánku sondou nic neteče
  digitalWrite(bias, LOW);
  digitalWrite(VREF_ON_PIN, LOW);
  digitalWrite(VDIV_ON_PIN, LOW);
  digitalWrite(LED_PIN, LOW);

  // Firmware po OTA, který se ještě nepotvrdil (nikdo neotevřel stránku, slabá baterie, Wi-Fi se nespustila),
  // se NEUKLÁDÁ do spánku: po probuzení by ho bootloader vrátil na předchozí firmware, a ten (verze 3) držení
  // pinů z hlubokého spánku nepouští, takže BIAS sondy, dělič baterie i LED by zůstaly zadržené v LOW až do
  // odpojení baterie. Restart (ještě bez držení pinů) vrátí předchozí firmware hned a čistě.
  if (guardUnconfirmed()) esp_restart();

  // Reed stále sepnutý (magnet ponechán u zařízení)? Pak by probuzení "při sepnutí"
  // nastalo hned a zařízení by se donekonečna budilo. Místo toho čekáme na rozepnutí.
  pinMode(WAKE_PIN, INPUT_PULLUP);
  delay(2);
  const bool held = (digitalRead(WAKE_PIN) == LOW);
  waitRelease = held;
  esp_sleep_enable_ext0_wakeup((gpio_num_t)WAKE_PIN, held ? 1 : 0);
  // Vnitřní pull-up v RTC doméně (po esp_sleep_enable_ext0_wakeup, jinak se přepíše):
  // bez něj by vstup v hlubokém spánku mohl plavat a budit zařízení náhodně.
  rtc_gpio_pullup_en((gpio_num_t)WAKE_PIN);
  rtc_gpio_pulldown_dis((gpio_num_t)WAKE_PIN);

  if (timerSec > 0) esp_sleep_enable_timer_wakeup((uint64_t)timerSec * 1000000ULL);

  for (size_t i = 0; i < sizeof(kHoldLow) / sizeof(kHoldLow[0]); i++) {
    rtc_gpio_init(kHoldLow[i]);
    rtc_gpio_set_direction(kHoldLow[i], RTC_GPIO_MODE_OUTPUT_ONLY);
    rtc_gpio_set_level(kHoldLow[i], 0);
    rtc_gpio_hold_en(kHoldLow[i]);
  }
  rtc_gpio_init((gpio_num_t)bias);
  rtc_gpio_set_direction((gpio_num_t)bias, RTC_GPIO_MODE_OUTPUT_ONLY);
  rtc_gpio_set_level((gpio_num_t)bias, 0);
  rtc_gpio_hold_en((gpio_num_t)bias);

  esp_deep_sleep_start();
  for (;;) {  // sem se řízení nikdy nedostane
  }
}
