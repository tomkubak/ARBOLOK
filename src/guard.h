// ============================================================================
//  guard.h  –  stav ochrany před zabrickováním v paměti RTC (obal nad bootguard.h)
//  Používá jen RTC_NOINIT paměť: žádný flash, žádná NVS, žádný I²C – nesmí záviset na ničem,
//  co by mohlo být právě rozbité.
// ============================================================================
#pragma once
#include "bootguard.h"

// Hned na začátku setup(): zaeviduje příčinu startu a rozhodne, jestli se spustí nouzový režim.
bootguard::Decision guardBoot(bootguard::Cause cause, bool reedHeld);

void guardPhase(bootguard::Phase p);
void guardHealthy();    // cyklus úspěšně dokončen (těsně před uspáním)
void guardWebStable();  // web běží dost dlouho
void guardClear();      // čistý štít: po úspěšném OTA, při ručním návratu do normálního režimu

// Hlídací časovač: (znovu) natáhne odpočet timeoutMs. Neozve-li se program (další volání) do té doby, zařízení se
// záměrně shodí (abort -> pád -> restart), takže zaseknutý program nezůstane viset donekonečna a pád zapracuje do
// ochrany před zabrickováním. Do hlubokého spánku se odpočet nepřenáší. Volat jen z hlavního tasku.
void guardAlive(uint32_t timeoutMs);

// Potvrdí běžící firmware po OTA (esp_ota_mark_app_valid_cancel_rollback). Dokud firmware nepotvrdí
// sám sebe, bootloader (je-li v něm podpora návratu) při PRVNÍM resetu vrátí předchozí firmware.
// Nepotvrzený firmware = ještě se neprokázalo, že umí nastartovat web, obsloužit klienta a chvíli běžet.
// Volat MIMO kritickou sekci (zapisuje do flash). Chyba (už potvrzeno / bootloader bez podpory) nevadí.
// Nepotvrdí nic, když je právě nahraný NOVÝ obraz a čeká na restart (rollback-API by potvrdilo ten, ještě
// nespuštěný).
void guardConfirm();

// true = běžící firmware je po OTA ještě nepotvrzený (stav PENDING_VERIFY): při příštím resetu ho bootloader
// vrátí na předchozí firmware. Takový firmware se nesmí uspat s drženými piny (viz powerSleep), ať se předchozí
// firmware nespustí s piny zadrženými v LOW. false i když stav nejde zjistit / bootloader nemá rollback.
bool guardUnconfirmed();

// Zapamatovaná odpověď guardUnconfirmed() pro stránku (zjistí se při startu a po každém pokusu o potvrzení;
// nečte při každém dotazu flash). true = zápatí stránky ukazuje "(trial)": zkušební provoz po OTA.
bool guardTrial();
