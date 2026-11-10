#!/usr/bin/env python3
# Test tools/check_bin.py na syntetických obrazech ESP32 (stejná struktura jako skutečný firmware.bin)
# a – je-li zadána proměnná ARBOLOG_REAL_BIN – i na skutečném obrazu (např. sestavení verze 3).
#   python3 -I tests_host/test_check_bin.py
import contextlib
import hashlib
import io
import os
import random
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import check_bin as cb  # noqa: E402

FAILS = []
CHECKS = 0


def check(cond, what):
    global CHECKS
    CHECKS += 1
    if not cond:
        FAILS.append(what)
        print("  SELHALO:", what)


def make_image(seg_lens=(4096, 1000), chip=0, hash_appended=1, appdesc=True, safe=True, version=b"v4.2",
               total_len=None, seed=1):
    """Sestaví obraz ve formátu ESP-IDF. total_len = přesná délka souboru (upraví se délka prvního úseku)."""
    rnd = random.Random(seed)
    seg_lens = list(seg_lens)
    if total_len is not None:
        # soubor = 24 + 8 + len0 [+ další úseky] + výplň + 1 + 32; pro jeden úsek: p = total-33, len0 = p - 32
        seg_lens = [total_len - (33 if hash_appended else 1) - 32]
    segs = []
    for i, ln in enumerate(seg_lens):
        d = bytearray(rnd.getrandbits(8) for _ in range(min(ln, 5000)))
        d += bytearray(b"\x5a" * (ln - len(d)))
        if i == 0:
            if appdesc:
                desc = bytearray(256)
                struct.pack_into("<I", desc, 0, cb.APP_DESC_MAGIC)
                desc[112:112 + 8] = b"v5.4.1\0\0"
                d[0:256] = desc
            else:
                d[0:4] = b"\0\0\0\0"
            blob = b"\0" + version + b"\0" + (cb.SAFE_MARK if safe else b"") + b"\0"
            d[300:300 + len(blob)] = blob
        segs.append(bytes(d))
    hdr = bytearray(24)
    hdr[0], hdr[1], hdr[2], hdr[3] = 0xE9, len(segs), 2, 0x20
    struct.pack_into("<I", hdr, 4, 0x400828EC)
    struct.pack_into("<H", hdr, 12, chip)
    hdr[23] = hash_appended
    img = bytearray(hdr)
    x = 0xEF
    for i, d in enumerate(segs):
        img += struct.pack("<II", 0x3F400020 + i * 0x100000, len(d)) + d
        for b in d:
            x ^= b
    pad = (15 - len(img)) % 16
    img += b"\0" * pad
    img.append(x)
    if hash_appended:
        img += hashlib.sha256(bytes(img)).digest()
    if total_len is not None and len(img) != total_len:
        raise AssertionError("pomocná funkce: délka %d != požadovaná %d (total_len musí být násobek 16)"
                             % (len(img), total_len))
    return bytearray(img)


def run(img, ver="v4.2"):
    return cb.inspect(bytes(img), ver)


def has(lst, word):
    return any(word in s for s in lst)


# --- 0. pevné konstanty (nesmí se změnit bez rozmyslu) ---------------------------------------------
# OTA stránka volá Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000); slot app0/app1 = 0x140000.
check(cb.OTA_SLOT == 0x140000 and cb.OTA_MAX == ((cb.OTA_SLOT - 0x1000) & 0xFFFFF000) == 1306624,
      "OTA_MAX = 1 306 624 B (slot 0x140000 minus 4 kB)")
check(cb.APP_DESC_MAGIC == 0xABCD5432 and cb.SAFE_MARK == b"ARBOLOG-SAFEMODE", "značky a magic konstanty")

# --- 1. platný obraz ---------------------------------------------------------------------------
e, w, i = run(make_image())
check(not e, "platný obraz: žádná chyba (%r)" % e)
check(not w, "platný obraz: žádné varování (%r)" % w)
check(has(i, "nouzový režim") and has(i, "v4.2") and has(i, "v5.4.1"), "platný obraz: info o verzi, IDF a nouzovém režimu")

e, w, i = run(make_image(seg_lens=(2000, 3000, 77, 1)))
check(not e and not w, "více úseků (výplň i liché délky): v pořádku (%r %r)" % (e, w))
for ln in range(1, 40):  # všechny zbytky délky modulo 16
    e, w, i = run(make_image(seg_lens=(300 + ln,)))
    check(not e, "délka úseku %d: v pořádku (%r)" % (300 + ln, e))

# --- 2. poškození ------------------------------------------------------------------------------
img = make_image()
img[1500] ^= 0x01
e, w, i = run(img)
check(has(e, "kontrolní součet") and has(e, "SHA-256"), "změněný bajt: chyba součtu i SHA (%r)" % e)

img = make_image()  # oprava XOR součtu, SHA zůstane špatné
img[1500] ^= 0x01
n = len(img)
cs_pos = n - 33
img[cs_pos] ^= 0x01
e, w, i = run(img)
check(not has(e, "kontrolní součet") and has(e, "SHA-256"), "opravený součet, špatné SHA: jen chyba SHA (%r)" % e)

img = make_image()
img[-1] ^= 0xFF
e, w, i = run(img)
check(has(e, "SHA-256") and not has(e, "kontrolní součet"), "změněný poslední bajt SHA (%r)" % e)

img = make_image()
e, w, i = run(img[:len(img) // 2])
check(has(e, "useknutý"), "useknutý na polovině (%r)" % e)
e, w, i = run(img[:-10])
check(has(e, "useknutý"), "chybí konec SHA (%r)" % e)
e, w, i = run(img[:-33])
check(has(e, "useknutý") or has(e, "SHA-256"), "chybí celé SHA (%r)" % e)
e, w, i = run(img[:40])
check(len(e) > 0, "40 B: chyba")
e, w, i = run(b"")
check(has(e, "příliš malý"), "prázdný soubor")
e, w, i = run(b"\xe9" * 100)
check(len(e) > 0, "samé 0xE9: nějaká chyba (%r)" % e)

# vlastnost: u obrazu s SHA-256 musí odhalit změnu KAŽDÉHO bitu v celém souboru (SHA kryje hlavičku, úseky,
# výplň i kontrolní součet; změna samotného SHA také). Jediná výjimka: příznak "SHA připojeno" (posun 23, bit 0),
# po jeho shození ESP-IDF obraz také přijme (ověřuje jen XOR součet), proto je to jen varování.
small = make_image(seg_lens=(200, 101))
missed = []
for off in range(len(small)):
    for bit in range(8):
        if off == 23 and bit == 0:
            continue
        m = bytearray(small)
        m[off] ^= 1 << bit
        e_, _w_, _i_ = run(m)
        if not e_:
            missed.append((off, bit))
check(not missed, "každá změna bitu v obrazu je odhalena (%d bitů, neodhaleno %r)" % (len(small) * 8, missed[:5]))

# vlastnost: žádný vstup nesmí vyhodit výjimku (náhodný šum, náhodně useknuté a pomíchané obrazy)
rnd = random.Random(2024)
base_img = make_image(seg_lens=(900, 300, 5))
crashed = None
for k in range(3000):
    kind = k % 3
    if kind == 0:
        data = bytes(rnd.getrandbits(8) for _ in range(rnd.randint(0, 400)))
    elif kind == 1:
        data = bytes(base_img[:rnd.randint(0, len(base_img))])
    else:
        m = bytearray(base_img)
        for _ in range(rnd.randint(1, 6)):
            m[rnd.randrange(len(m))] = rnd.getrandbits(8)
        data = bytes(m)
    if kind == 0 and data[:1] != b"\xe9" and k % 2:
        data = b"\xe9" + data[1:]  # polovina náhodných souborů má správný první bajt
    try:
        cb.inspect(data, "v4.2")
    except Exception as exc:  # noqa: BLE001
        crashed = (k, repr(exc))
        break
check(crashed is None, "inspect() nikdy nevyhodí výjimku (náhodné vstupy): %r" % (crashed,))

# --- 3. špatný typ souboru -----------------------------------------------------------------------
fs = bytearray(random.Random(7).getrandbits(8) for _ in range(5000))
fs[0] = 0x00
e, w, i = run(fs)
check(has(e, "není to obraz aplikace"), "souborový systém (první bajt není 0xE9)")
part = bytearray(b"\xaa\x50\x01\x02") + bytearray(3068)
e, w, i = run(part)
check(has(e, "není to obraz aplikace"), "partitions.bin")
e, w, i = run(make_image(chip=5))
check(has(e, "ESP32-C3"), "obraz pro jiný čip (%r)" % e)
e, w, i = run(make_image(chip=99))
check(has(e, "čip"), "neznámý čip")
e, w, i = run(make_image(appdesc=False))
check(has(e, "popis aplikace"), "bez popisu aplikace (bootloader) (%r)" % e)
img = make_image()
img[1] = 0
e, w, i = run(img)
check(has(e, "počet úseků"), "nulový počet úseků")
img[1] = 17
e, w, i = run(img)
check(has(e, "počet úseků"), "17 úseků")
img = make_image()
struct.pack_into("<I", img, 28, 0x7FFFFFFF)  # délka prvního úseku nesmysl
e, w, i = run(img)
check(has(e, "úsek 0"), "nesmyslná délka úseku nespadne a hlásí chybu (%r)" % e)

# --- 4. velikost: přesná hranice ------------------------------------------------------------------
e, w, i = run(make_image(total_len=cb.OTA_MAX))
check(not e and has(w, "95"), "přesně OTA_MAX bajtů projde (s varováním o zaplnění) (%r %r)" % (e, w))
e, w, i = run(make_image(total_len=cb.OTA_MAX + 16))
check(has(e, "přijme nejvýš"), "o 16 B víc než OTA_MAX je chyba (%r)" % e)
e, w, i = run(make_image(total_len=(int(cb.OTA_MAX * 0.90) // 16) * 16))
check(not e and not w, "pod 95 %% limitu: bez varování (%r %r)" % (e, w))
check(len(make_image(total_len=cb.OTA_MAX)) == cb.OTA_MAX, "pomocná funkce staví přesnou délku")

# --- 5. co je to za firmware ------------------------------------------------------------------------
e, w, i = run(make_image(safe=False))
check(not e and has(w, "není to firmware verze 4"), "bez nouzového režimu: varování, ne chyba (%r %r)" % (e, w))
e, w, i = run(make_image(version=b"v3.9"))
check(not e and has(w, "v4.2"), "jiná verze: varování, ne chyba (%r %r)" % (e, w))
e, w, i = run(make_image(version=b"xv4.2"))
check(has(w, "v4.2"), "verze musí být celý řetězec (xv4.2 není v4.2)")
e, w, i = run(make_image(), None)
check(not e and not w, "bez zadané verze se verze nehledá")

# --- 6. SHA nepřipojeno / navíc ---------------------------------------------------------------------------
e, w, i = run(make_image(hash_appended=0))
check(not e and has(w, "SHA-256"), "bez SHA: varování, ne chyba (%r %r)" % (e, w))
img = make_image()
e, w, i = run(img + b"\0" * 64)
check(not e and has(w, "navíc"), "bajty navíc na konci: varování (%r %r)" % (e, w))

# --- 7. report() a main() ------------------------------------------------------------------------------------
tmp = os.environ.get("TMPDIR", "/tmp")
os.makedirs(tmp, exist_ok=True)
good = os.path.join(tmp, "arbolog_check_good.bin")
bad = os.path.join(tmp, "arbolog_check_bad.bin")
with open(good, "wb") as f:
    f.write(make_image())
broken = make_image()
broken[2000] ^= 0x10
with open(bad, "wb") as f:
    f.write(broken)
buf = io.StringIO()
check(cb.report(good, "v4.2", buf) is True, "report(): dobrý soubor -> True")
check("NAHRÁVEJTE" not in buf.getvalue() and "platný obraz" in buf.getvalue(), "report(): text pro dobrý soubor")
buf = io.StringIO()
check(cb.report(bad, "v4.2", buf) is False, "report(): poškozený soubor -> False")
check("NENAHRÁVEJTE" in buf.getvalue(), "report(): text pro špatný soubor")
buf = io.StringIO()
check(cb.report(os.path.join(tmp, "neexistuje.bin"), None, buf) is False and "CHYBA" in buf.getvalue(),
      "report(): chybějící soubor")
class AsciiOnly(io.StringIO):
    """Terminál, který neumí české znaky: write() vyhodí UnicodeEncodeError jako při LANG=C."""
    def write(self, s):
        s.encode("ascii")
        return super().write(s)


ao = AsciiOnly()
check(cb.report(good, "v4.2", ao) is True and "?" in ao.getvalue() and "KONTROLA" in ao.getvalue(),
      "report(): terminál bez UTF-8 nevyhodí výjimku, znaky se nahradí")
with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):  # ať test nestraší výpisy
    rc_good = cb.main(["check_bin.py", good])
    rc_bad = cb.main(["check_bin.py", bad])
    rc_both = cb.main(["check_bin.py", good, bad])
    rc_none = cb.main(["check_bin.py"])
check(rc_good == 0, "main(): dobrý soubor -> 0")
check(rc_bad == 1, "main(): špatný soubor -> 1")
check(rc_both == 1, "main(): jeden špatný ze dvou -> 1")
check(rc_none == 2, "main(): bez argumentu -> 2")
check(cb.fw_version_from_config(os.path.join(HERE, "..")) is not None, "FW_VERSION se přečte z src/config.h")
for p in (good, bad):
    os.remove(p)

# --- 8. skutečný obraz (volitelně) ----------------------------------------------------------------------------------
real = os.environ.get("ARBOLOG_REAL_BIN")
if real:
    with open(real, "rb") as f:
        data = f.read()
    e, w, i = cb.inspect(data, "v4.2")
    check(not e, "skutečný obraz %s: žádná chyba (%r)" % (real, e))
    check(has(i, "ESP-IDF"), "skutečný obraz: přečte se verze ESP-IDF")
    print("  skutečný obraz:", real, len(data), "B ->", "OK" if not e else e)
    # a všechny zásahy do skutečného souboru musí být odhaleny
    for off in (0, 1, 12, 100, len(data) // 2, len(data) - 40, len(data) - 1):
        bad_data = bytearray(data)
        bad_data[off] ^= 0x01
        e2, _w2, _i2 = cb.inspect(bytes(bad_data), None)
        check(len(e2) > 0, "skutečný obraz s poškozeným bajtem na posunu %d: chyba" % off)
    # příznak "SHA připojeno" (posun 23) 1 -> 0: ESP-IDF takový obraz také přijme (jen XOR součet), proto jen varování
    bad_data = bytearray(data)
    bad_data[23] ^= 0x01
    e2, w2, _i2 = cb.inspect(bytes(bad_data), None)
    check(not e2 and has(w2, "SHA-256"), "skutečný obraz bez příznaku SHA: varování (%r %r)" % (e2, w2))

print("test_check_bin: %d kontrol, %d selhalo" % (CHECKS, len(FAILS)))
if FAILS:
    sys.exit(1)
print("test_check_bin: OK, %d kontrol" % CHECKS)
