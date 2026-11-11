Hotový firmware ARBOLOK v7.0-rc1 (nahrává se přes http://10.10.10.10/update, postup v README.md, kapitola 5).

arbolok_v7.0-rc1.bin             výchozí sestavení (pin BIAS GPIO32, stará deska) – použijte tento
arbolok_v7.0-rc1_nova-deska.bin  výchozí pin BIAS GPIO26 (nová deska)

Oba fungují na obou deskách: správný pin BIAS si zařízení najde samo při prvním spuštění webu.
Kontrolní součty: SHA256SUMS.txt (ověření: shasum -a 256 -c SHA256SUMS.txt).
