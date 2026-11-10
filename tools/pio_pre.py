# PlatformIO pre-skript: před sestavením přegeneruje src/web_assets.h ze zdrojů ve web/.
# Selhání (např. chybí python3) sestavení nezastaví – použije se stávající web_assets.h.
Import("env")  # noqa: F821  (vkládá PlatformIO)

import os
import subprocess
import sys

proj = env["PROJECT_DIR"]  # noqa: F821
script = os.path.join(proj, "tools", "gen_web_assets.py")
try:
    subprocess.check_call([sys.executable, "-I", script], cwd=proj)
except Exception as exc:  # noqa: BLE001
    print("POZOR: web_assets.h se nepodařilo přegenerovat (%s) – použije se stávající." % exc)
