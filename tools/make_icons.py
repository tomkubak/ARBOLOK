#!/usr/bin/env python3
"""Vyrenderuje ikonu aplikace z web/icons/icon.svg do PNG (180 a 192 px; 512 jen pro náhled).
Potřebuje Playwright (pip install playwright && playwright install chromium) a Pillow. Běžně není potřeba:
PNG jsou v repozitáři hotová."""
import os
from PIL import Image
from playwright.sync_api import sync_playwright

D = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "web", "icons")
svg = open(os.path.join(D, "icon.svg"), encoding="utf-8").read()
with sync_playwright() as p:
    b = p.chromium.launch()
    for size, name in ((180, "apple-touch-icon.png"), (192, "icon-192.png"), (512, "icon-512.png")):
        pg = b.new_page(viewport={"width": size, "height": size})
        pg.set_content('<body style="margin:0">' + svg.replace("<svg ", '<svg width="%d" height="%d" ' % (size, size), 1) + "</body>")
        out = os.path.join(D, name)
        pg.screenshot(path=out, clip={"x": 0, "y": 0, "width": size, "height": size})
        Image.open(out).convert("RGB").quantize(colors=128, method=Image.Quantize.FASTOCTREE).save(out, optimize=True)
    b.close()
print("ikony hotové")
