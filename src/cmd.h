// ============================================================================
//  cmd.h  –  fronta příkazů z webu do hlavní smyčky
// ============================================================================
#pragma once
#include <stdint.h>

#include "locations.h"
#include "settings.h"

enum CmdType : uint8_t {
  CMD_NONE = 0,
  CMD_MEASURE,       // změř hned (živá hodnota)
  CMD_PROFILE,       // zachyť průběh napětí po zapnutí sondy
  CMD_CAL_DRY,       // aktuální stav = 0 %
  CMD_CAL_WET,       // aktuální stav = 100 %
  CMD_CAL_RESET,     // výchozí kalibrace
  CMD_CAL_VBAT,      // kalibrace napětí baterie podle multimetru (f = V)
  CMD_SET_TIME,      // i[0..5] = rok, měsíc, den, hod, min, s
  CMD_SET_SETTINGS,  // set + mask (SM_*) = které položky nastavení se mění
  CMD_EXTEND,        // prodluž web o SESSION_EXTEND_SEC
  CMD_FACTORY_RESET, // smaž nastavení v NVS (log zůstane) a restartuj
  CMD_RESTART,
  CMD_LOC,           // místa: i[0] = op (LOC_OP_*), i[1] = index, name = nové jméno
  CMD_LOCS,          // všechna místa najednou (import / úprava v Servisu); tabulka čeká v cmdStageLocs, i[0] = vybrané místo
  CMD_WIRING,        // test zapojení: který pin řídí BIAS sondy (wiring.h); nalezený se uloží a použije
  CMD_MANUAL,        // ruční zápis (přenosný logger): změř a zapiš do logu se značkou M, name = poznámka
  CMD_WIFI_PW        // heslo Wi-Fi (od v7.0): pw = nové heslo ("" = otevřená síť), uloží se a zařízení se restartuje
};

struct Cmd {
  CmdType type = CMD_NONE;
  float f = 0.0f;
  int32_t i[6] = {0, 0, 0, 0, 0, 0};
  uint32_t mask = 0;  // CMD_SET_SETTINGS: bity SM_* z settings.h
  Settings set{};  // vynulované (kopíruje se do fronty celé)
  char name[LOC_NAME + 1] = {0};  // CMD_LOC: jméno místa (už očištěné)
  char pw[64] = {0};              // CMD_WIFI_PW: heslo (už ověřené, webparse::validWifiPassword)
};

enum LocOp : int32_t { LOC_OP_SELECT = 1, LOC_OP_NEW = 2, LOC_OP_RENAME = 3, LOC_OP_DELETE = 4 };

void cmdInit();
bool cmdPost(const Cmd& c);  // z webových handlerů; false = fronta plná
bool cmdTake(Cmd& c);        // z loop(); false = prázdná
// Tabulka míst je pro frontu velká: web ji odloží sem a pošle CMD_LOCS. false = předchozí ještě nezpracovaná.
bool cmdStageLocs(const Location* ls, uint8_t n);
bool cmdTakeLocs(Location* ls, uint8_t& n);  // z loop() při CMD_LOCS
