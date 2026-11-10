// ============================================================================
//  locations.h  –  "místa": pojmenované kalibrační profily (kalibrace + prahy sucho/mokro)
//
//  Jedna sonda se dá používat na více místech (stromy, záhony, různé zeminy). Každé místo má vlastní kalibraci
//  sucho/mokro a vlastní prahy. Pracovní kopie aktivního místa je v Settings (cal, dryPct, wetPct) – zbytek
//  firmwaru se tak nemění; tabulka míst se při každém uložení nastavení srovná (locSyncFromSettings).
//  Záznamy v logu nesou číslo místa (" p<N>", N od 1), stránka podle něj ukazuje data vybraného místa.
//  NVS (jmenný prostor "arbolok"): "locn" = počet, "loc0".."loc7" = "dry;wet;gDry;gWet;rawOpen;jméno".
// ============================================================================
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "moisture.h"

const uint8_t LOC_MAX = 8;
const size_t LOC_NAME = 24;  // nejvýš bajtů UTF-8 (bez NUL)

struct Location {
  char name[LOC_NAME + 1] = {0};
  float gDry = 0.05f;
  float gWet = 3.0f;
  uint16_t rawOpen = 4095;
  uint8_t dry = 30;
  uint8_t wet = 85;
};

namespace loc {
// ČISTÉ funkce (testované na PC)
// Jméno místa: řídicí znaky a ; " \ < > se vynechají, mezery na krajích ořežou, délka nejvýš LOC_NAME bajtů
// (ořízne se jen na hranici znaku UTF-8, takže nevznikne rozbitý znak). Vrací délku.
size_t sanitizeName(const char* in, char* out, size_t outCap);
// "dry;wet;gDry;gWet;rawOpen;jméno" (jméno poslední, smí obsahovat cokoli kromě vyřazených znaků)
bool encode(const Location& l, char* out, size_t cap);
bool decode(const char* s, Location& l);  // false = nesmysl (rozsahy se kontrolují jako u nastavení)
// Smí se místo uložit? Jméno neprázdné, 0 <= sucho < mokro <= 100, platná kalibrace (g sucho > 0, g mokro > 1,2 × g sucho, < 1000).
bool valid(const Location& l);
}  // namespace loc

struct Settings;
void locFromSettings(const Settings& s, Location& l);  // cal + prahy (jméno zůstává)
void locToSettings(const Location& l, Settings& s);

// Tabulka míst (v RAM, ukládá se do NVS). Volat z hlavní smyčky; čtení z webového tasku pod StateLock.
// Změny (locSet, locAdd, locRemove, locReplaceAll, locSyncFromSettings) mění JEN tabulku v RAM – smí se tedy volat
// pod StateLock. Do NVS je zapíše až locSave(), a to se volá MIMO StateLock: zápis do flash v kritické sekci
// (vypnutá přerušení, druhé jádro čeká na zámek) může skončit zablokováním obou jader a pádem na watchdog.
void locLoadAll(const Settings& s);  // načte; prázdná tabulka -> jedno místo "Místo 1" z aktuálního nastavení
uint8_t locCount();
const Location& locGet(uint8_t i);
bool locSet(uint8_t i, const Location& l);  // přepíše (false = neplatný index)
bool locAdd(const Location& l);             // na konec (false = plno)
bool locRemove(uint8_t i);                  // false = poslední místo nejde smazat
bool locReplaceAll(const Location* ls, uint8_t n);  // import / úprava všech míst najednou (1..LOC_MAX, platná)
bool locSyncFromSettings(const Settings& s);  // aktivní místo <- nastavení; true = změnilo se
bool locSave();  // neuložené změny tabulky do NVS (MIMO StateLock); true = zapisovalo se
