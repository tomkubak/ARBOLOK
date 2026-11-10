# PlatformIO post-skript:
#  1) PŘED kontrolou velikosti, kterou dělá PlatformIO, vypíše velikost firmwaru z linkerové mapy (tools/size_report.py):
#     kolik zbývá do limitu OTA, co je největší a jestli se kód nepřeložil bez optimalizace. Vypíše se i když se
#     firmware nevejde (to PlatformIO ukončí chybou "program size ... is greater than maximum allowed").
#  2) PO sestavení zkontroluje firmware.bin (platný obraz ESP32, velikost vůči tomu, co přijme OTA stránka /update,
#     firmware verze 4 s nouzovým režimem) a vypíše, který soubor se nahrává (tools/check_bin.py; jde spustit i ručně:
#     python3 tools/check_bin.py soubor.bin).
# Selhání skriptu sestavení NIKDY nezastaví a výpis nikdy nespadne na kódování terminálu.
Import("env")  # noqa: F821  (vkládá PlatformIO)

import os
import sys

PROJ = env["PROJECT_DIR"]  # noqa: F821
_size_printed = []


def _say(text):
    try:
        print(text)
    except Exception:  # noqa: BLE001  (terminál bez UTF-8: ASCII náhrada)
        try:
            print(text.encode("ascii", "replace").decode("ascii"))
        except Exception:  # noqa: BLE001
            pass


def _tools_on_path():
    tools = os.path.join(PROJ, "tools")
    if tools not in sys.path:
        sys.path.insert(0, tools)


def _size(target, source, env):  # noqa: ANN001  (SCons volá funkci s pojmenovanými argumenty)
    if _size_printed:
        return
    path = "firmware.map"
    try:
        path = os.path.join(env.subst("$BUILD_DIR"), (env.subst("${PROGNAME}") or "firmware") + ".map")
        if not os.path.isfile(path):
            return
        _tools_on_path()
        import size_report

        _size_printed.append(True)
        size_report.report(path)
    except Exception as exc:  # noqa: BLE001
        _say("POZOR: výpis velikosti selhal (%s); sestavení je v pořádku. Zkuste ručně: python3 tools/size_report.py %s"
             % (exc, path))


def _report(source, target, env):  # noqa: ANN001
    path = "firmware.bin"
    try:
        path = str(target[0])
        if not os.path.isfile(path):
            return
        _tools_on_path()
        import check_bin

        check_bin.report(path, check_bin.fw_version_from_config(PROJ))
    except Exception as exc:  # noqa: BLE001
        _say("POZOR: automatická kontrola firmware.bin selhala (%s); sestavení je v poradku. "
             "Zkuste rucne: python3 tools/check_bin.py %s" % (exc, path))


try:
    # před kontrolou velikosti od PlatformIO (ta při překročení slotu sestavení ukončí)
    env.AddPreAction("checkprogsize", _size)  # noqa: F821
except Exception as exc:  # noqa: BLE001
    _say("POZOR: výpis velikosti před kontrolou se nepodařilo zapojit (%s); vypíše se až po sestavení." % exc)

# po sestavení: velikost (jen když se ještě nevypsala) a kontrola samotného souboru
env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", _size)  # noqa: F821
env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", _report)  # noqa: F821
