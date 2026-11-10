// ============================================================================
//  webui.h  –  webový server: vložená stránka + HTTP API (ESPAsyncWebServer)
//
//  Handlery běží v tasku AsyncTCP. Na hardware (ADC, I²C, NVS) NESAHAJÍ – čtou
//  kopii stavu (state.h) a změny posílají hlavní smyčce příkazem (cmd.h).
//  Výjimka: soubory (výpis, stažení, smazání, nahrání) obsluhují přímo.
//  Popis API: docs/API.md
// ============================================================================
#pragma once

void webuiBegin();            // zaregistruje handlery a spustí server (po startu AP)
void webuiPoll();             // volat z loop(): hlídá zapomenutý příznak OTA
bool webuiTransferActive();   // běží stahování/nahrávání/OTA -> neuspávat
bool webuiFsBusy();           // někdo čte soubor z flash -> nemazat/nezkracovat log
bool webuiServed();           // server od startu obsloužil aspoň jeden požadavek stránky (důkaz, že web funguje)
