#!/usr/bin/env bash
# Sestaví a spustí host testy čistého jádra (bez ESP32, stačí g++ a python3).
#   tools/run_host_tests.sh
set -euo pipefail
cd "$(dirname "$0")/.."
OUT="${TMPDIR:-/tmp}/arbolog_host_tests"
rm -rf "$OUT"
mkdir -p "$OUT/json"
# Úroveň optimalizace lze změnit: ARBOLOG_OPT=-Os bash tools/run_host_tests.sh  (firmware se překládá s -Os,
# viz platformio.ini; kód se tak ověří i v té podobě, v jaké poběží na desce).
FLAGS="-std=gnu++17 ${ARBOLOG_OPT:--O1} -g -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -fno-sanitize-recover=undefined"

echo "== jádro: měření, log, baterie, nastavení =="
g++ $FLAGS tests_host/test_core.cpp src/moisture.cpp -o "$OUT/test_core"
"$OUT/test_core"

echo
echo "== web: plánovač, JSON, parsování =="
g++ $FLAGS tests_host/test_web.cpp src/moisture.cpp -o "$OUT/test_web"
ARBOLOG_JSON_OUT="$OUT/json" "$OUT/test_web"

echo
echo "== nastavení: NVS (převzetí z v3, kruhový průchod, poškozená NVS) =="
g++ $FLAGS -Itests_host/stubs -Isrc tests_host/test_settings.cpp src/settings.cpp src/locations.cpp src/moisture.cpp -o "$OUT/test_settings"
"$OUT/test_settings"

echo
echo "== logstore: soubory v paměťové atrapě LittleFS (zápis, zředění, plný disk, obnova po startu) =="
g++ $FLAGS -Itests_host/fsmem -Itests_host/stubs -Isrc tests_host/test_logstore.cpp src/logstore.cpp -o "$OUT/test_logstore"
"$OUT/test_logstore"

echo
echo "== probe: skutečná měřicí smyčka nad virtuálními hodinami a simulovanou sondou =="
g++ $FLAGS -Itests_host/stubs -Isrc tests_host/test_probe.cpp src/probe.cpp src/moisture.cpp -o "$OUT/test_probe"
"$OUT/test_probe"

echo
echo "== wiring: rozpoznání pinu BIAS (rozhodnutí z čítačů uzlu) =="
g++ $FLAGS tests_host/test_wiring.cpp -o "$OUT/test_wiring"
"$OUT/test_wiring"

echo
echo "== battery: měření článku nad atrapou ADC =="
g++ $FLAGS -Itests_host/stubs -Isrc tests_host/test_battery.cpp src/battery.cpp -o "$OUT/test_battery"
"$OUT/test_battery"

echo
echo "== bootguard: ochrana před zabrickováním (nouzový režim, scénáře pádů, časovač) =="
g++ $FLAGS tests_host/test_bootguard.cpp -o "$OUT/test_bootguard"
"$OUT/test_bootguard"

echo
echo "== config.h: neplatné heslo Wi-Fi se nesmí přeložit (zařízení bez Wi-Fi by bez kabelu nešlo opravit) =="
pw_fail=0
for len in 0 1 7 8 20 63 64 70; do
  pw=""
  if [ "$len" -gt 0 ]; then pw="$(head -c "$len" < /dev/zero | tr '\0' 'a')"; D="-DAP_PASSWORD=\"$pw\""; else D=""; fi
  if echo '#include "config.h"' | g++ -std=gnu++17 -fsyntax-only -x c++ -Isrc $D - 2>/dev/null; then got=prejde; else got=selze; fi
  if [ "$len" -eq 0 ] || { [ "$len" -ge 8 ] && [ "$len" -le 63 ]; }; then want=prejde; else want=selze; fi
  if [ "$got" = "$want" ]; then echo "  ok    heslo o délce $len: $got"; else echo "  CHYBA heslo o délce $len: $got (mělo: $want)"; pw_fail=1; fi
done
[ "$pw_fail" -eq 0 ]

echo
echo "== validace JSON v Pythonu =="
python3 -I tools/check_json.py "$OUT/json"

echo
echo "== check_bin: kontrola .bin před nahráním přes OTA (syntetické obrazy; ARBOLOG_REAL_BIN = i skutečný obraz) =="
TMPDIR="$OUT" python3 -I -B tests_host/test_check_bin.py

echo
echo "== size_report: výpis velikosti z linkerové mapy a jeho zapojení do PlatformIO (ARBOLOG_REAL_MAP + ARBOLOG_REAL_MAP_SIZE = i skutečná mapa) =="
TMPDIR="$OUT" python3 -I -B tests_host/test_size_report.py
