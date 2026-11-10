#!/usr/bin/env python3
# Kontrola souboru PŘED nahráním přes OTA (zařízení, které jde nahrát jen přes Wi-Fi, nemá záchranu kabelem).
#
#   python3 tools/check_bin.py .pio/build/esp32doit-devkit-v1/firmware.bin
#
# Ověří, že soubor je platný obraz aplikace pro ESP32 (hlavička, počet úseků, kontrolní součet, SHA-256),
# že se vejde mezi to, co přijme stránka /update, a že je to firmware verze 4 s nouzovým režimem
# (ne littlefs.bin / bootloader.bin / partitions.bin, ne stará verze 3). Soubor jen čte.
# Volá se i automaticky po sestavení (tools/pio_post.py). Výjimka: nic nenahrává a nic nemění.
#
# Formát obrazu (ESP-IDF, esp_image_format.h): 24 B hlavička, pak `segment_count` úseků [adresa, délka, data],
# za posledním úsekem výplň do 16 B, jejíž poslední bajt je XOR všech dat (seed 0xEF), a (je-li příznak
# hash_appended) 32 B SHA-256 všeho před tím. Na posunu 32 B leží popis aplikace (magic 0xABCD5432).
import hashlib
import os
import re
import struct
import sys

# Největší obraz, který stránka /update přijme: knihovna ESPAsyncHTTPUpdateServer volá
# Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000), tedy slot 0x140000 minus 4 kB.
# (Výchozí tabulka oddílů pro 4 MB flash: app0 i app1 mají 0x140000 = 1 310 720 B.)
OTA_MAX = 0x13F000
OTA_SLOT = 0x140000

APP_DESC_MAGIC = 0xABCD5432
SAFE_MARK = b"ARBOLOG-SAFEMODE"  # vložen do stránky nouzového režimu (src/safemode.cpp), NEMAZAT
CHIPS = {0: "ESP32", 2: "ESP32-S2", 5: "ESP32-C3", 9: "ESP32-S3", 12: "ESP32-C2", 13: "ESP32-C6", 16: "ESP32-H2"}


def _cstr(raw):
    return raw.split(b"\0", 1)[0].decode("latin-1")


def inspect(data, fw_version=None):
    """Vrací (chyby, varování, info) jako seznamy vět. Žádná chyba = soubor je použitelný obraz aplikace."""
    err, warn, info = [], [], []
    n = len(data)
    if n < 64:
        err.append("soubor je příliš malý (%d B), není to firmware" % n)
        return err, warn, info
    if data[0] != 0xE9:
        err.append("není to obraz aplikace ESP32 (první bajt není 0xE9). Do formuláře Firmware se nahrává "
                   "firmware.bin, ne littlefs.bin, partitions.bin ani jiný soubor")
        return err, warn, info

    seg_count = data[1]
    chip = struct.unpack_from("<H", data, 12)[0]
    hash_appended = data[23]
    if chip != 0:
        err.append("obraz je pro čip %s, ne pro ESP32" % CHIPS.get(chip, "s označením %d" % chip))
    if not 1 <= seg_count <= 16:
        err.append("neplatný počet úseků v hlavičce (%d)" % seg_count)
        return err, warn, info

    # úseky + XOR kontrolní součet
    pos = 24
    xor = 0xEF
    for i in range(seg_count):
        if pos + 8 > n:
            err.append("soubor je useknutý (končí uprostřed hlavičky úseku %d). Nahrávání nebo kopírování se "
                       "nedokončilo, soubor vytvořte znovu" % i)
            return err, warn, info
        _load, length = struct.unpack_from("<II", data, pos)
        if length > n - pos - 8:
            err.append("soubor je useknutý (úsek %d má mít %d B, ale do konce souboru jich zbývá %d). "
                       "Nahrávání nebo kopírování se nedokončilo, soubor vytvořte znovu"
                       % (i, length, n - pos - 8))
            return err, warn, info
        for b in data[pos + 8:pos + 8 + length]:
            xor ^= b
        pos += 8 + length

    cs_pos = pos + ((15 - pos) % 16)  # poslední bajt výplně do 16 B
    if cs_pos >= n:
        err.append("soubor je useknutý (chybí kontrolní součet na konci)")
        return err, warn, info
    if data[cs_pos] != xor:
        err.append("kontrolní součet obrazu nesedí (soubor je poškozený)")
    if hash_appended:
        if n < cs_pos + 33:
            err.append("soubor je useknutý (chybí SHA-256 na konci)")
        else:
            if hashlib.sha256(data[:cs_pos + 1]).digest() != data[cs_pos + 1:cs_pos + 33]:
                err.append("SHA-256 obrazu nesedí (soubor je poškozený)")
            if n > cs_pos + 33:
                warn.append("za obrazem je %d B navíc (podpis?)" % (n - cs_pos - 33))
    else:
        warn.append("obraz nemá připojený SHA-256 (neobvyklé); platnost se ověřila jen XOR součtem")

    # popis aplikace (jen Arduino/IDF aplikace ho mají; bootloader.bin ne)
    if struct.unpack_from("<I", data, 32)[0] == APP_DESC_MAGIC:
        idf = _cstr(data[32 + 112:32 + 144])
        if idf:
            info.append("sestaveno proti ESP-IDF %s" % idf)
    else:
        err.append("chybí popis aplikace – nejspíš to není aplikace (bootloader.bin?)")

    # velikost
    pct = 100.0 * n / OTA_MAX
    if n > OTA_MAX:
        err.append("soubor má %d B, stránka /update přijme nejvýš %d B (nahrávání by skončilo chybou, "
                   "zařízení by zůstalo na staré verzi)" % (n, OTA_MAX))
    else:
        info.append("velikost %d B = %.1f %% toho, co OTA přijme (%d B)" % (n, pct, OTA_MAX))
        if pct > 95.0:
            warn.append("firmware zabírá přes 95 % limitu OTA; další přidávání kódu by OTA znemožnilo")

    # co je to za firmware
    if SAFE_MARK in data:
        info.append("obsahuje nouzový režim (firmware verze 4)")
    else:
        warn.append("není to firmware verze 4 (chybí nouzový režim). Pokud nahráváte záměrně starou verzi "
                    "(návrat na v3), je to v pořádku; jinak jde o špatný soubor")
    if fw_version:
        if b"\0" + fw_version.encode("ascii", "replace") + b"\0" in data:
            info.append("verze v souboru: %s" % fw_version)
        else:
            warn.append("řetězec verze %s se v souboru nenašel (je to opravdu čerstvé sestavení?)" % fw_version)
    return err, warn, info


def fw_version_from_config(project_dir):
    """Přečte FW_VERSION z src/config.h (nebo None)."""
    try:
        with open(os.path.join(project_dir, "src", "config.h"), "r", encoding="utf-8") as f:
            m = re.search(r'#define\s+FW_VERSION\s+"([^"]+)"', f.read())
        return m.group(1) if m else None
    except OSError:
        return None


def format_report(path, fw_version=None):
    """Sestaví text zprávy o souboru. Vrací (v_pořádku, text); v_pořádku = žádná chyba."""
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError as exc:
        return False, "CHYBA: soubor %s nejde přečíst (%s)\n" % (path, exc)
    err, warn, info = inspect(data, fw_version)
    bar = "=" * 72
    lines = [bar, "KONTROLA PŘED NAHRÁNÍM PŘES OTA: %s" % path,
             "SHA-256 souboru: %s" % hashlib.sha256(data).hexdigest()]
    lines += ["  OK      %s" % s for s in info]
    lines += ["  POZOR   %s" % s for s in warn]
    lines += ["  CHYBA   %s" % s for s in err]
    if err:
        lines.append("VÝSLEDEK: tento soubor NENAHRÁVEJTE.")
    else:
        lines.append("VÝSLEDEK: soubor je platný obraz aplikace pro ESP32%s."
                     % ("" if not warn else " (přečtěte si POZOR výše)"))
        lines.append("Nahrává se na http://10.10.10.10/update, formulář Firmware; po nahrání otevřete http://10.10.10.10/.")
    lines.append(bar)
    return not err, "\n".join(lines) + "\n"


def report(path, fw_version=None, out=None):
    """Vypíše zprávu o souboru. Vrací True, když je soubor v pořádku (žádná chyba).
    Výpis nikdy nevyhodí výjimku kvůli kódování terminálu (české znaky se pak nahradí)."""
    out = out or sys.stdout
    ok, text = format_report(path, fw_version)
    try:
        out.write(text)
    except UnicodeError:
        try:
            out.write(text.encode("ascii", "replace").decode("ascii"))
        except Exception:  # noqa: BLE001
            pass
    return ok


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    if not args:
        sys.stderr.write("použití: python3 tools/check_bin.py soubor.bin [...]\n")
        return 2
    proj = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ver = fw_version_from_config(proj)
    ok = True
    for p in args:
        ok = report(p, ver) and ok
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
