// ============================================================================
//  Arbolog / ARBOLOK  –  konfigurace (piny, konstanty, ladicí parametry)
//  Všechno, co se dá "ladit", je tady na jednom místě.
// ============================================================================
#pragma once
#include <stdint.h>

#define FW_VERSION "v7.0"

// ---------------------------------------------------------------------------
//  Varianta desky (nastavuje se v platformio.ini přes -DARBOLOG_BOARD=1|2)
//    1 = stará deska  (bias sondy na GPIO32, druhá elektroda na GND)
//    2 = nová deska   (bias sondy na GPIO26, DAC2; druhá elektroda na GPIO14)
//  Jsou to VÝCHOZÍ piny nového zařízení a po továrním nastavení (od v7.0 se uloží rovnou, bez rozpoznávání);
//  změnit jdou ve webu (Nastavení -> Zapojení sondy, volba "Automaticky" pin BIAS zase rozpozná za běhu).
// ---------------------------------------------------------------------------
#ifndef ARBOLOG_BOARD
#define ARBOLOG_BOARD 1
#endif

#define LED_PIN 13
#define VDIV_ON_PIN 12  // GPIO12 je strapping pin (MTDI) – při resetu NESMÍ být HIGH
#define VREF_ADC_PIN 33
#define VDIV_ADC_PIN 34
#define VREF_ON_PIN 27
#define SONDA_ADC_PIN 35
#define WAKE_PIN 4  // jazýčkové relé (reed) proti GND, probouzí z deep sleep
#if ARBOLOG_BOARD == 2
#define SONDA_BIAS_PIN 26
#define SONDA_RET_PIN 14  // druhá elektroda na GPIO14 (wiring::kRetPins)
#else
#define SONDA_BIAS_PIN 32
#define SONDA_RET_PIN 0  // druhá elektroda napevno na GND
#endif

#define REF_VOLTAGE 1.235f  // přesná referenční dioda (V)

// ---------------------------------------------------------------------------
//  Wi-Fi / web
// ---------------------------------------------------------------------------
// Heslo AP zadané PŘI SESTAVENÍ (min. 8 znaků = WPA2). Prázdné = otevřená síť (výchozí).
// Stejné heslo se použije pro stránku /update (uživatel "admin") a platí i v nouzovém režimu.
// Běžně se heslo nastavuje ve webu (Servis -> Wi-Fi, od v7.0, uloženo v NVS); to má přednost, platí jen v běžném
// režimu a nouzový režim magnetem ho umí smazat (zapomenuté heslo), viz safemode.cpp.
#ifndef AP_PASSWORD
#define AP_PASSWORD ""
#endif
// Neplatné heslo (1-7 znaků, nebo víc než 63) by WiFi.softAP() odmítlo: zařízení by v běžném i nouzovém režimu
// nikdy nespustilo Wi-Fi a bez kabelu by se nedalo opravit. Proto se takové sestavení vůbec nepřeloží.
static_assert(sizeof(AP_PASSWORD) == 1 || (sizeof(AP_PASSWORD) >= 9 && sizeof(AP_PASSWORD) <= 64),
              "AP_PASSWORD musi mit 8 az 63 znaku (nebo byt prazdne = otevrena sit)");
#define AP_SSID_PREFIX "ARBOLOK-"  // do v6.16 "ARBOL0K-" (s nulou)
#define AP_CHANNEL 1
#define AP_MAX_CLIENTS 4
#define MDNS_HOSTNAME "arbolok"
// Vysílací výkon AP. Telefon je u zařízení v dosahu pár metrů; nižší výkon než výchozích ~19,5 dBm
// (verze 3) = nižší proudové špičky při napájení přímo z LiFePO4 článku (3,0–3,4 V bez stabilizátoru).
// 15 dBm je kompromis: dosah desítek metrů ve volném prostoru, špičky o ~50 mA nižší než na maximu.
// Stejné nastavení používá i nouzový režim, takže co funguje v běžném webu, funguje i v něm.
#define WIFI_TX_POWER_DBM_ENUM WIFI_POWER_15dBm
// Nízký výkon (Servis -> Zobrazení a Wi-Fi -> Vysílač Wi-Fi: "Nízký"), telefon je u stromu. Ve v7.0-rc1/rc2 byl
// výchozí; od v7.0 je výchozí zase normální výkon (settingsDefaults), nízký jde zapnout ve webu.
// Nouzový režim ho nepoužívá (tam má přednost dosah).
#define WIFI_TX_LOW_ENUM WIFI_POWER_8_5dBm

// ---------------------------------------------------------------------------
//  Úspora baterie (v6.5)
// ---------------------------------------------------------------------------
// Web skončí dřív, než vyprší nastavená doba, když se po přiložení magnetu do WEB_NOCLIENT_SEC nikdo nepřipojí
// (magnet omylem, telefon se nepřipojil), nebo když od odpojení posledního telefonu uplyne WEB_IDLE_SEC.
#define WEB_NOCLIENT_SEC 90UL
#define WEB_IDLE_SEC 60UL
#define WEB_CPU_MHZ 80             // Wi-Fi funguje od 80 MHz; v4.x–v6.4 web běžel na 160 MHz
// Šetřící režim: pod VBAT_SAVE_V (změřeno při záznamu, bez zátěže Wi-Fi) se interval záznamu zdvojnásobí,
// nad VBAT_SAVE_OFF_V se vrátí. Stránka to ohlásí.
#define VBAT_SAVE_V 3.10f
#define VBAT_SAVE_OFF_V 3.20f
// Plán záznamů: jedno probuzení na záznam (viz timeutil.h). Cíl spánku je kousek ZA hranicí intervalu.
#define SCHED_AIM_AFTER_SEC 2UL
// Odhad spotřeby v Servisu (počítá stránka z čítačů doby bdění; jsou to ODHADY, ne měření):
#define BOOT_OVERHEAD_MS 150UL     // bootloader a start před setup() (millis() je nezapočítá)
#define BAT_CAPACITY_MAH 2000      // kapacita článku pro odhad výdrže

// ---------------------------------------------------------------------------
//  Ochrana proti "zabrickování" (firmware se nahrává jen přes OTA) – viz bootguard.h
// ---------------------------------------------------------------------------
#define REED_SAFE_HOLD_MS 2000UL   // magnet držený u relé při PŘIPOJENÍ baterie tuto dobu = nouzový režim
#define WEB_STABLE_MS 45000UL      // web běžící aspoň tak dlouho se považuje za "stabilní" (pád po něm nevyvolá nouzový režim hned)
// Hlídací časovač (guard.h, guardAlive): když program visí déle než tato doba, záměrně se shodí (abort) a
// restartuje; pád se započítá do ochrany výše (nouzový režim / návrat na předchozí firmware). Bez něj by zaseknuté
// zařízení do vybití baterie svítilo a přes OTA by se nedalo opravit.
#define DEADMAN_BOOT_MS 90000UL    // od startu (a od konce webové relace) do uspání; vejde se i první formátování souborového systému
#define DEADMAN_LOOP_MS 60000UL    // v hlavní smyčce (web, nouzový režim): smyčka se musí ozvat aspoň takto často

// ---------------------------------------------------------------------------
//  Výchozí uživatelská nastavení (po prvním startu; pak se ukládají do NVS)
// ---------------------------------------------------------------------------
#define DEF_WEB_RUNTIME_MIN 4
#define DEF_INTERVAL_MIN 30   // záznam po 30 min (0 = vypnutý)
#define INTERVAL_MAX_MIN 120  // nejdelší volitelný interval záznamu
#define DEF_LIVE_SEC 15                 // jak často se měří "živá" hodnota při webu (s); od v6.11 v Servisu 5–120 s
#define SESSION_EXTEND_SEC 300
#define SESSION_MAX_SEC (30 * 60)
#define SESSION_MAX_MAN_SEC (60 * 60)  // se zapnutým ručním zápisem (obcházení stromů) smí stránka běžet až hodinu
#define MANUAL_PULSES 3                 // ruční zápis: medián ze 3 pulzů (každý zápis je jiné místo, bez ověřování)

// ---------------------------------------------------------------------------
//  Měření sondy
// ---------------------------------------------------------------------------
// Sonda je DC dělič (pull-up z BIAS pinu -> uzel -> ADC, elektroda -> GND).
// Napětí se po sepnutí BIAS chvíli ustaluje (RC uzlu, polarizace elektrod).
// Místo pevných 300 ms hledáme "plató": okna se zdvojující délky, a jakmile se
// mediány dvou po sobě jdoucích oken liší o <= PROBE_EPS_COUNTS, máme hotovo.
#define PROBE_T0_US 1500UL        // začátek prvního okna po sepnutí (přeskočí zákmit)
#define PROBE_MAX_US 300000UL     // tvrdý strop délky jednoho měření (jako v3: 300 ms)
#define PROBE_EPS_COUNTS 5.0f     // tolerance ustálení v ADC čítačích (z 4095)
#define PROBE_MIN_SAMPLES 10      // minimum vzorků v okně
#define PROBE_WIN_MAX_SAMPLES 128 // kolik vzorků okno ukládá (zbytek se vynechá)
#define PROBE_PREDISCHARGE_MS 30  // BIAS = LOW před pulzem (vybití elektrod)
#define PROBE_REPEAT_GAP_MS 800   // pauza mezi pulzy při opakovaném měření
#define PROBE_CAL_PULSES 3        // kolik pulzů se mediánuje při kalibraci
#define PROBE_EARLY_MIN_US 300    // režim "krátký pulz": meze délky (µs) a výchozí 1 ms
#define PROBE_EARLY_MAX_US 20000
#define DEF_EARLY_US 1000
#define PROBE_MAXMS_MIN 20        // meze nastavitelného stropu pulzu (ms)
#define PROBE_MAXMS_MAX 1000
#define DEF_CONFIRM_DELTA 150     // skok čítače, od kterého se jednopulzový záznam ověří
#define DEF_WJUMP_PCT 8           // detekce zalití (stránka): nárůst aspoň o 8 %
#define DEF_WWIN_H 6              // ... během 6 h

// Test zapojení (rozpoznání pinu BIAS, wiring.h): jak dlouho jeden kandidát drží BIAS nahoře, pauza na vybití a počet čtení
#define WIRING_PULSE_MS 25
#define WIRING_GAP_MS 20
#define WIRING_READS 9

// Výchozí kalibrace (do prvního ručního nastavení v UI)
#define DEF_CAL_G_DRY 0.05f   // index vodivosti pro 0 %   (raw ~3900 při rawOpen 4095)
#define DEF_CAL_G_WET 3.0f    // index vodivosti pro 100 % (raw ~1020)
#define DEF_CAL_RAW_OPEN 4095 // ADC čítač naprázdno (sonda ve vzduchu)
// Pull-up děliče sondy (BIAS -> uzel). Ve zdroji dané hardwarem: 10 kΩ (nižší hodnoty měly pro zeminu
// malou citlivost). Slouží k přepočtu indexu vodivosti g = R/Rp na odpor zeminy Rp (jen pro zobrazení).
#define DEF_PULLUP_KOHM 10.0f
// ESP32 je napájeno přímo z LiFePO4 (3,0–3,5 V), BIAS = napětí článku. ADC má pevnou vnitřní referenci
// (NENÍ poměrové k napájení), takže čítač na uzlu roste a klesá s napětím článku -> korekce je ve
// výchozím stavu zapnutá (viz moist::normalizeRaw).
#define DEF_VEXC_TRACK true
#define VEXC_REF_VOLTS 3.30f  // napětí buzení, ke kterému jsou kalibrace vztaženy

// ---------------------------------------------------------------------------
//  Baterie (LiFePO4, jeden článek)
// ---------------------------------------------------------------------------
#define VBAT_DEF_CORRECTION 0.97f  // korekce děliče (v3.1s: 0.97)
#define VBAT_SETTLE_MS 50
#define VBAT_SAMPLES 16
#define VBAT_REF_MIN_COUNTS 500    // rozumné meze ADC čítače pro 1.235 V referenci
#define VBAT_REF_MAX_COUNTS 2600
#define VBAT_WARN_V 3.00f          // pod tím UI hlásí slabou baterii
#define VBAT_NOWIFI_V 2.90f        // pod tím se Wi-Fi vůbec nezapíná (brownout riziko)
#define VBAT_NOLOG_V 2.70f         // pod tím se přestane i logovat (ochrana článku) a zařízení přejde do hibernace
// Hibernace (ochrana hluboce vybitého článku): pod VBAT_NOLOG_V se zařízení budí jen jednou za VBAT_HIBERNATE_SEC,
// změří baterii a spí dál; Wi-Fi se nezapne (magnet = 10 bliknutí). Normální provoz se vrátí až nad VBAT_RESUME_V
// (hystereze: článek, který se po odlehčení jen trochu vzpamatuje, se hned znovu nezatěžuje).
#define VBAT_RESUME_V 3.00f
#define VBAT_HIBERNATE_SEC (6UL * 3600UL)

// ---------------------------------------------------------------------------
//  Log (LittleFS)
// ---------------------------------------------------------------------------
#define LOG_PATH "/log.txt"
#define LOG_TMP_PATH "/log.tmp"
#define UP_TMP_PATH "/up.tmp"            // dočasný soubor při nahrávání přes web
#define LOG_MAX_BYTES (600UL * 1024UL)  // nad tím se starší polovina zředí (do v6.12 560 kB)
#define LOG_MIN_FREE_BYTES (48UL * 1024UL)
#define LOG_EMERGENCY_KEEP_BYTES (96UL * 1024UL)
// Kvůli nedostatku VOLNÉHO místa se log zřeďuje jen tehdy, když je aspoň tak velký: zředění uvolní
// jen ~25 % logu. U menšího logu místo zabírají jiné (nahrané) soubory a opakované přepisování
// by jen ničilo záznamy a opotřebovávalo flash.
#define LOG_COMPACT_MIN_BYTES (64UL * 1024UL)
// Jak dlouho smí webový task čekat na zámek logu (zředění velkého logu trvá vteřiny).
#define LOG_WEB_LOCK_WAIT_MS 500UL
// Nahrávaný soubor musí nechat volné místo pro zpracování logu: LOG_MIN_FREE_BYTES + 75 % logu
// (dočasný soubor při zředění) + tato rezerva.
#define UPLOAD_HEADROOM_BYTES (8UL * 1024UL)

// ---------------------------------------------------------------------------
//  Ladění po sériové lince (jen ve vývoji: -DARBOLOG_DEBUG=1)
// ---------------------------------------------------------------------------
#ifndef ARBOLOG_DEBUG
#define ARBOLOG_DEBUG 0
#endif
