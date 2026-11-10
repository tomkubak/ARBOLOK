// ============================================================================
//  safemode.cpp  –  NOUZOVÝ REŽIM: jen Wi-Fi AP + OTA aktualizace firmwaru
//
//  Spouští se po opakovaných pádech programu, po pádu při startu webu, nebo ručně magnetem při
//  připojení baterie (rozhodnutí: bootguard.h). Smysl: i s rozbitým firmwarem jde nahrát opravu
//  přes Wi-Fi.
//
//  ÚMYSLNĚ nepoužívá nic z běžného provozu: ani nastavení (NVS), ani sondu a ADC, baterii,
//  hodiny (I²C), LittleFS, ani webovou stránku a API (webui.cpp) – kterákoli z těch částí může být
//  právě ta rozbitá. Používá jen to, co je k OTA nezbytné: Wi-Fi, AsyncWebServer a updater
//  (a čistou funkci webparse::sameOrigin, bez stavu a bez hardwaru).
//
//  Chování:
//    * AP "ARBOL0K-SAFE-<číslo>" (stejné heslo jako v běžném režimu, když je nastavené), IP 10.10.10.10
//    * stránka http://10.10.10.10/ (vysvětlení + odkaz na /update + tlačítko "Zkusit normální režim")
//    * /update = stejná OTA stránka jako v běžném režimu
//    * LED: dvojité bliknutí každé 2 s
//    * po 10 min bez připojeného telefonu usne (probudí ho magnet), po 60 min vždy
//    * po podpětí (slabá baterie) Wi-Fi nezapíná vůbec: jen 3× bliká a spí
// ============================================================================
#include "safemode.h"

#include <Arduino.h>
#include <ESPAsyncHTTPUpdateServer.h>
#include <ESPAsyncWebServer.h>
#include <Update.h>
#include <WiFi.h>

#include <stdio.h>
#include <string.h>

#include "bootguard.h"
#include "config.h"
#include "guard.h"
#include "power.h"
#include "webparse.h"

namespace {

AsyncWebServer* s_srv = nullptr;
// Globální objekt: knihovna si člen _updateResult neinicializuje, spoléhá na nulování statické paměti.
ESPAsyncHTTPUpdateServer s_upd;

volatile bool s_otaActive = false;
volatile uint32_t s_otaStartMs = 0;
volatile uint32_t s_restartAtMs = 0;  // 0 = restart nežádán
bool s_forced = false;
char s_id[12] = "0000000";

const char kPage[] =
    "<!doctype html><html lang=\"cs\"><head><meta charset=\"utf-8\">"
    "<!--ARBOLOG-SAFEMODE-->"  // značka pro tools/check_bin.py (pozná firmware s nouzovým režimem), NEMAZAT
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>ARBOLOK – nouzový režim</title>"
    "<style>body{font:16px/1.45 system-ui,sans-serif;max-width:34em;margin:1em auto;padding:0 1em;color:#222}"
    "h1{font-size:1.3em}.b{display:block;box-sizing:border-box;width:100%;margin:.7em 0;padding:.85em;"
    "border:0;border-radius:.5em;background:#1b7f4b;color:#fff;font:inherit;text-align:center;"
    "text-decoration:none}.g{background:#555}small{color:#666}.b small{color:#e6e6e6}</style></head><body>"
    "<h1>ARBOLOK – nouzový režim <small>(safe mode)</small></h1>"
    "<p>Zařízení <b>@ID@</b>, firmware <b>@FW@</b>.<br>@WHY@</p>"
    "<p>Měření, záznam i běžná stránka jsou vypnuté, aby šlo nahrát (opravený) firmware.</p>"
    "<a class=\"b\" href=\"/update\">Nahrát firmware (OTA)<br><small>Upload firmware – soubor firmware.bin</small></a>"
    "<form method=\"post\" action=\"/normal\"><button class=\"b g\" type=\"submit\">Zkusit normální režim<br>"
    "<small>Try normal mode (restart)</small></button></form>"
    "<p><small>Nouzový režim se po 10 min bez připojeného telefonu sám vypne (spánek, probudí ho magnet). "
    "Úplný reset: odpojte a znovu připojte baterii.</small></p></body></html>";

void handlePage(AsyncWebServerRequest* r) {
  String page(kPage);
  page.replace("@ID@", s_id);
  page.replace("@FW@", FW_VERSION);
  page.replace("@WHY@", s_forced ? "Spuštěno magnetem při připojení baterie."
                                 : "Běžný program opakovaně padal, proto se spustil nouzový režim.");
  r->send(200, "text/html; charset=utf-8", page.c_str());
}

void handleNormal(AsyncWebServerRequest* r) {
  r->send(200, "text/html; charset=utf-8",
          "<meta charset=\"utf-8\"><meta http-equiv=\"refresh\" content=\"25;url=/\">"
          "Restartuji do normálního režimu… / Restarting…");
  guardClear();  // čistý štít; padne-li běžný program znovu, vrátí se sem
  s_restartAtMs = (millis() + 800UL) | 1u;  // odpověď musí stihnout odejít
}

void onOtaBegin(const UpdateType type, int& result) {
  (void)type;
  if (Update.isRunning()) Update.abort();  // přerušený předchozí pokus by blokoval nový
  s_otaStartMs = millis() | 1u;
  s_otaActive = true;
  result = UPDATE_OK;  // bez toho zůstává stav v knihovně neinicializovaný
}

void onOtaEnd(const UpdateType type, int& result) {
  if (result == UPDATE_OK) {
    if (type == FIRMWARE) guardClear();  // nový firmware dostane čistý štít (zařízení se za chvíli restartuje)
  } else {
    s_otaActive = false;  // chyba: dá se zkusit znovu
  }
}

[[noreturn]] void sleepReedOnly() {
  bool waitRelease = false;
  powerSleep(0, waitRelease);  // bez časovače: probudí jen magnet
}

}  // namespace

[[noreturn]] void safeModeRun(BootKind kind, bool forced, bool giveUp) {
  powerInitPins();  // LED + relé s pull-upem

  if (giveUp || kind == BK_BROWNOUT) {
    // Wi-Fi by slabou baterii dorazilo, resp. smyčka pádů by ji vybíjela: jen ohlásit a spát.
    if (giveUp) powerBlink(6, 80, 80);
    else powerBlink(3, 120, 150);
    sleepReedOnly();
  }

  s_forced = forced;
  powerSerial(s_id, sizeof(s_id));
  setCpuFrequencyMhz(160);

  char ssid[32];
  snprintf(ssid, sizeof(ssid), "%sSAFE-%s", AP_SSID_PREFIX, s_id);
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  WiFi.setTxPower(WIFI_TX_POWER_DBM_ENUM);
  WiFi.softAPConfig(IPAddress(10, 10, 10, 10), IPAddress(10, 10, 10, 10), IPAddress(255, 255, 255, 0));
  bool apOk = false;
  for (int i = 0; i < 3 && !apOk; i++) {
    apOk = WiFi.softAP(ssid, AP_PASSWORD[0] ? AP_PASSWORD : nullptr, AP_CHANNEL, 0, 2);
    if (!apOk) delay(500);
  }
  if (!apOk) {
    powerBlink(5, 400, 200);
    sleepReedOnly();  // restart by jen donekonečna točil Wi-Fi a vybíjel baterii
  }
  WiFi.setTxPower(WIFI_TX_POWER_DBM_ENUM);  // znovu po startu AP (viz main.cpp)

  s_srv = new AsyncWebServer(80);
  s_upd.onUpdateBegin = onOtaBegin;
  s_upd.onUpdateEnd = onOtaEnd;
  // Knihovna čte parametr "name" bez kontroly -> POST na /update bez něj by zařízení shodil (viz webui.cpp).
  s_srv->on("/update", HTTP_POST, [](AsyncWebServerRequest* r) { r->send(400, "text/plain", "missing name"); })
      .setFilter([](AsyncWebServerRequest* r) { return !r->hasParam("name"); });
  // Firmware z cizí stránky (Origin jiný než Host) se odmítne, stejně jako v běžném režimu (webui.cpp).
  s_srv->on("/update", HTTP_POST, [](AsyncWebServerRequest* r) { r->send(403, "text/plain", "origin"); })
      .setFilter([](AsyncWebServerRequest* r) {
        return r->hasHeader("Origin") &&
               !webparse::sameOrigin(r->header("Origin").c_str(), r->header("Host").c_str());
      });
  if (AP_PASSWORD[0]) s_upd.setup(s_srv, "admin", AP_PASSWORD);
  else s_upd.setup(s_srv);
  s_srv->on("/", HTTP_GET, handlePage);
  s_srv->on("/index.html", HTTP_GET, handlePage);
  s_srv->on("/normal", HTTP_POST, handleNormal);
  s_srv->onNotFound([](AsyncWebServerRequest* r) { r->redirect("/"); });
  s_srv->begin();

  const uint32_t start = millis();
  uint32_t lastBusy = start;
  uint32_t lastKick = 0;
  for (;;) {
    const uint32_t now = millis();
    if ((uint32_t)(now - lastKick) >= 1000UL) {
      lastKick = now;
      guardAlive(DEADMAN_LOOP_MS);  // zaseknutý nouzový režim = pád -> po 6 pádech se Wi-Fi přestane zapínat
    }
    const bool otaRunning = s_otaActive && (uint32_t)(now - s_otaStartMs) <= bootguard::kSafeOtaMs;
    if (WiFi.softAPgetStationNum() > 0 || otaRunning) lastBusy = now;

    digitalWrite(LED_PIN, bootguard::safeLedOn(now) ? HIGH : LOW);

    const uint32_t rs = s_restartAtMs;
    if (rs != 0 && (int32_t)(now - rs) >= 0) {
      digitalWrite(LED_PIN, LOW);
      ESP.restart();
    }
    if (bootguard::safeExpired(now, start, lastBusy, s_otaActive, s_otaStartMs)) break;
    delay(20);
  }

  digitalWrite(LED_PIN, LOW);
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  sleepReedOnly();
}
