Hotový firmware ARBOLOK v7.0.

Aktualizace přes web (http://10.10.10.10/update, postup v README.md, kapitola 5):
arbolok_v7.0.bin             stará deska: BIAS GPIO32, druhá elektroda na GND
arbolok_v7.0_nova-deska.bin  nová deska:  BIAS GPIO26, druhá elektroda na GPIO14

Nahrání přes USB do prázdného ESP32 (celý čip, adresa 0x0):
arbolok_v7.0_usb-cely.bin             stará deska
arbolok_v7.0_nova-deska_usb-cely.bin  nová deska
Obsahují bootloader, tabulku oddílů i aplikaci. Nahrání v Chromu přes
https://espressif.github.io/esptool-js/ (adresa 0x0), nebo esptoolem:
  esptool.py --chip esp32 --port /dev/cu.usbserial-XXXX write_flash 0x0 arbolok_v7.0_usb-cely.bin
Na už použitém ESP32 nejdřív smazat flash (erase_flash), jinak zůstane staré nastavení.
Když se deska nepřipojí, podržet při startu nahrávání tlačítko BOOT.

Oba: výchozí normální výkon Wi-Fi (15 dBm), logaritmická stupnice, krátký pulz.
Výchozí hodnoty platí pro nové zařízení a po továrním nastavení; zapojení jde změnit ve webu (Servis -> Sonda a měření).
Kontrolní součty: SHA256SUMS.txt (ověření: shasum -a 256 -c SHA256SUMS.txt).
