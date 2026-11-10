#!/bin/bash
# ============================================================================
#  Arbolog: aktualizace projektu z nového zipu + sestavení + kontrola .bin (macOS / Linux)
#
#  Na Macu stačí na tento soubor dvakrát kliknout (poprvé: pravé tlačítko -> Otevřít, macOS se zeptá).
#  Co udělá:
#   1. vezme nejnovější Arbolog_4*.zip ze složky Stažené (nebo zip zadaný jako parametr),
#   2. přepíše jím tento projekt NA MÍSTĚ (složky src, web, tools, testy se srovnají přesně podle zipu;
#      .pio s překladem zůstane, takže další sestavení je rychlé; vaše vlastní soubory mimo tyto složky zůstanou),
#   3. spustí sestavení (pio run), ukáže velikost a zkontroluje firmware.bin (tools/check_bin.py),
#   4. zkopíruje hotový firmware na Plochu jako arbolog_vX.Y.bin – ten pak nahrajete přes OTA stránku.
#  Nic nenahrává do zařízení a nic nemaže mimo složky projektu.
# ============================================================================
main() {
  set -uo pipefail
  cd "$(dirname "$0")" || exit 1
  local proj ZIP tmp src pio ver out
  proj="$(pwd)"
  ZIP="${1:-$(ls -t "$HOME"/Downloads/Arbolog_4*.zip "$HOME"/Stažené/Arbolog_4*.zip 2>/dev/null | head -1)}"
  if [ -z "$ZIP" ] || [ ! -f "$ZIP" ]; then
    echo "Nenašel jsem Arbolog_4*.zip ve Stažených. Přetáhněte zip do okna Terminálu za název skriptu, nebo ho stáhněte."
    return 1
  fi
  echo "== Zip: $ZIP"
  tmp="$(mktemp -d)"
  trap 'rm -rf "$tmp"' RETURN
  unzip -q "$ZIP" -d "$tmp" || { echo "Zip nejde rozbalit."; return 1; }
  src="$tmp/Arbolog_4"
  [ -f "$src/platformio.ini" ] && [ -f "$src/src/config.h" ] || { echo "V zipu není projekt Arbolog_4."; return 1; }
  # Kódové složky přesně podle zipu (zmizí i soubory, které nová verze už nemá – jinak by se překládaly staré .cpp).
  local d f
  for d in src web tools tests_host tests_web docs; do
    if [ -d "$src/$d" ]; then
      rm -rf "${proj:?}/$d" && cp -Rp "$src/$d" "$proj/$d" || { echo "Kopírování $d selhalo."; return 1; }
    fi
  done
  # Ostatní soubory v kořeni (platformio.ini, README, tento skript...) jen přepsat; .pio, .vscode a vaše soubory zůstanou.
  for f in "$src"/* "$src"/.[!.]*; do
    [ -e "$f" ] || continue
    case "$(basename "$f")" in src|web|tools|tests_host|tests_web|docs|.pio|.vscode) continue ;; esac
    cp -Rp "$f" "$proj/" || { echo "Kopírování $(basename "$f") selhalo."; return 1; }
  done
  ver="$(sed -n 's/^#define FW_VERSION "\(.*\)".*/\1/p' "$proj/src/config.h")"
  echo "== Projekt aktualizován na $ver"

  pio="$(command -v pio || true)"
  [ -z "$pio" ] && [ -x "$HOME/.platformio/penv/bin/pio" ] && pio="$HOME/.platformio/penv/bin/pio"
  if [ -z "$pio" ]; then
    echo "Nenašel jsem PlatformIO (pio). Otevřete projekt ve VS Code a dejte Build, dál postup z README 1.1."
    return 1
  fi
  echo "== Sestavení ($pio run)"
  "$pio" run || { echo "!! Sestavení selhalo – pošlete výpis výše. (Nic se nenahrálo, nic se nerozbilo.)"; return 1; }
  out="$proj/.pio/build/esp32doit-devkit-v1/firmware.bin"
  python3 "$proj/tools/check_bin.py" "$out" || { echo "!! Kontrola .bin neprošla – NENAHRÁVAT, pošlete výpis."; return 1; }
  cp "$out" "$HOME/Desktop/arbolog_${ver}.bin" && echo "== Hotovo: na Ploše je arbolog_${ver}.bin – nahrajte ho na http://10.10.10.10/update"
}
# Celé tělo je ve funkci: bash ho načte celé dřív, než rsync tento soubor přepíše novou verzí.
main "$@"
rc=$?
echo
read -r -p "Hotovo (kód $rc). Enter zavře okno." _
exit $rc
