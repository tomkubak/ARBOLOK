// ============================================================================
//  safemode.h  –  NOUZOVÝ REŽIM: jen Wi-Fi AP + OTA aktualizace firmwaru
//
//  Smysl: i když je běžný program rozbitý (padá, visí), jde přes Wi-Fi nahrát opravu a zařízení,
//  které nemá sériový převodník, se nezabricknou. Kdy se spouští: viz bootguard.h.
// ============================================================================
#pragma once
#include <stdint.h>

#include "power.h"

// Nevrací se: buď se zařízení restartuje (po OTA / "Zkusit normální režim"), nebo usne.
// forced = spuštěno magnetem při připojení baterie (jen pro text na stránce).
// giveUp = ani nouzový režim neustál (opakované pády): Wi-Fi se nezapíná, zařízení jen spí na magnet.
[[noreturn]] void safeModeRun(BootKind kind, bool forced, bool giveUp);
