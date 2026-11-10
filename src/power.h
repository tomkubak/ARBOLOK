// ============================================================================
//  power.h  –  probuzení, držení pinů a deep sleep
//
//  Co řeší oproti verzi 3:
//   * řídicí piny (BIAS sondy, napájení děliče/reference, LED) se v hlubokém
//     spánku drží v LOW – nic neplave a nic netoče proud do elektrod/děliče;
//   * zapomenutý magnet u jazýčkového relé: zařízení se nezacyklí (po sepnutí čeká
//     na rozepnutí, místo aby se budilo pořád dokola);
//   * rozlišení příčiny startu (reed / časovač / zapnutí / pád / podpětí).
// ============================================================================
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "bootguard.h"

enum BootKind : uint8_t {
  BK_POWER = 0,   // zapnutí napájení, restart z webu, flashování
  BK_REED = 1,    // probuzení magnetem (jazýčkové relé)
  BK_TIMER = 2,   // probuzení časovačem (záznam)
  BK_CRASH = 3,   // reset po pádu / watchdogu
  BK_BROWNOUT = 4 // reset kvůli podpětí (slabá baterie)
};

BootKind powerBootKind();
// Jemnější rozlišení příčiny startu pro ochranu před zabrickováním (bootguard.h):
// zapnutí napájení / restart z webu (OTA) / probuzení ze spánku / pád / podpětí.
bootguard::Cause powerBootCause();
// Je jazýčkové relé sepnuté (magnet) po celých holdMs? Vrací false hned při prvním rozepnutí, takže bez
// magnetu start zdrží jen o ~5 ms (ustálení pull-upu). Za sepnutí svítí LED (zpětná vazba).
bool powerReedHeld(uint32_t holdMs);
void powerReleaseHolds();   // hned na začátku setup(): uvolní piny držené z deep sleep
void powerInitPins();       // LED, reed, nastavení ADC
void powerBlink(uint8_t n, uint16_t onMs, uint16_t offMs);
// Sériové číslo zařízení (7 číslic z MAC v eFuse) – stejné v běžném i nouzovém režimu (název Wi-Fi).
void powerSerial(char* out, size_t cap);

// Uspí zařízení. timerSec == 0 -> bez časovače (jen reed). waitRelease se vyplní
// podle toho, jestli je reed v okamžiku uspání stále sepnutý (ukládá volající do RTC RAM).
[[noreturn]] void powerSleep(uint32_t timerSec, bool& waitRelease);
