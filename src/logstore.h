// ============================================================================
//  logstore.h  –  záznam měření do LittleFS (/log.txt)
//
//  - jeden řádek = jeden zápis (žádné rozdělené print() volání)
//  - hlídá velikost souboru i volné místo; při překročení starší polovinu zředí
//  - přerušené zředění (výpadek napájení) se při startu automaticky dořeší
//
//  Vlákna: zápis a zředění dělá hlavní smyčka, smazání/nahrazení/čtení logu z webu
//  dělá task AsyncTCP. Všechno je serializované rekurzivním mutexem. Web na něj čeká
//  JEN omezenou dobu (zředění velkého logu trvá vteřiny, async_tcp ale nesmí stát) –
//  když ho nezíská, operace skončí s busy = true a handler odpoví 409/503 "busy".
// ============================================================================
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "logfmt.h"

bool logMount();                              // LittleFS.begin(), při selhání formát
bool logAppend(const logfmt::Record& r);      // false = nezapsáno (plný disk apod.)

// Smazání / nahrazení logu z webového tasku. *busy (volitelné) = true: zámek logu se nepodařilo
// získat včas (hlavní smyčka právě zapisuje nebo ředí) – nic se nestalo, zkusit znovu.
bool logClear(bool* busy = nullptr);
bool logReplaceWith(const char* tmpPath, bool* busy = nullptr);  // atomicky nahradí log hotovým souborem
// Tovární nastavení (od v7.0-rc1): zformátuje celý souborový systém (log i nahrané soubory). Volá jen hlavní smyčka
// těsně před restartem; rozdělané stahování nechá nejvýš waitMs doběhnout. false = formát selhal.
bool logFormatAll(uint32_t waitMs);

size_t logSizeBytes();
size_t fsTotalBytes();
size_t fsUsedBytes();
int logRemoveLegacy();                        // smaže nepotřebné soubory staré stránky (chart.js, style.css); vrací počet
bool logCompactNow();                         // ruční zředění; false = nezředěno (i kdyby se právě četlo)
// Přečíslování míst v logu (logfmt::remapPlaceLine; map[1..nOld]). false = nepřepsáno: log se právě stahuje,
// nebo není místo na dočasný soubor (velikost logu + 8 kB). Log, který neexistuje, = true.
bool logRemapPlaces(const uint8_t* map, uint8_t nOld);

// Webová vrstva hlásí, že právě někdo čte soubor z flash (stahování). Dokud to trvá, logger
// log nezkracuje ani nepřepisuje (zředění odloží; zapíše jen, když se záznam vejde).
typedef bool (*LogReaderProbe)();
void logSetReaderProbe(LogReaderProbe fn);

// Krátký kritický úsek nad logem pro webový task (otevření souboru ke stahování a zapsání
// čtenáře do počítadla – viz webui.cpp). use = false: zámek se neberete a ok() je vždy true.
class LogGuard {
 public:
  LogGuard(bool use, uint32_t waitMs);
  ~LogGuard();
  bool ok() const { return ok_; }

 private:
  LogGuard(const LogGuard&) = delete;
  LogGuard& operator=(const LogGuard&) = delete;
  bool ok_;
  bool held_;
};
