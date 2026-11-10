#!/usr/bin/env bash
# Kontrola překladu firmwaru BEZ toolchainu ESP32: každý src/*.cpp projde `g++ -fsyntax-only`
# proti atrapám jádra Arduino-ESP32 (tests_host/stubs/) a proti SKUTEČNÝM hlavičkám knihoven
# (ESPAsyncWebServer, AsyncTCP, ESPAsyncHTTPUpdateServer, RTClib), takže se ověří typy,
# přetížení funkcí a názvy. NENÍ to náhrada za `pio run` (atrapy nejsou emulace jádra).
#
#   tools/syntax_check.sh                      # knihovny z .pio/libdeps/esp32doit-devkit-v1
#   LIBDEPS=/cesta/k/libdeps/xyz tools/syntax_check.sh
set -uo pipefail
cd "$(dirname "$0")/.."
LIB="${LIBDEPS:-.pio/libdeps/esp32doit-devkit-v1}"
if [ ! -d "$LIB/ESPAsyncWebServer/src" ]; then
  echo "Chybí knihovny v '$LIB' (vzniknou při prvním 'pio run'; nebo nastavte LIBDEPS)." >&2
  exit 2
fi
# Knihovny jako -isystem: varování uvnitř cizích hlaviček se nezobrazují.
INC=(-Itests_host/stubs -Isrc -isystem "$LIB/ESPAsyncWebServer/src" -isystem "$LIB/AsyncTCP/src"
     -isystem "$LIB/ESPAsyncHTTPUpdateServer/src" -isystem "$LIB/RTClib/src" -isystem "$LIB/Adafruit BusIO")
FLAGS=(-std=gnu++2a -fsyntax-only -fno-rtti -fexceptions -Wall -Wextra -Werror=return-type
       -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers
       -DESP32=1 -DARDUINO=10812 -DARDUINO_ARCH_ESP32 -DESPASYNCHTTPUPDATESERVER_MODE=1)
if [ -n "${EXTRA_FLAGS:-}" ]; then FLAGS+=($EXTRA_FLAGS); fi
fail=0
for f in src/*.cpp; do
  if g++ "${FLAGS[@]}" "${INC[@]}" "$f" 2> >(grep -v "^In file included\|^\s*from " >&2); then
    echo "  ok    $f"
  else
    echo "  CHYBA $f"; fail=1
  fi
done
exit $fail
