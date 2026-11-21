# Verkabelung

Aktueller Aufbau auf dem Steckbrett mit dem ESP32-C3-DevKitM-1
(Pinout: [esp32-c3-devkitm-1-v1-pinout.png](esp32-c3-devkitm-1-v1-pinout.png)).

Steckbrett-Plan: [breadboard-plan.png](breadboard-plan.png), Fritzing-Quelle:
[breadboard-plan.fzz](breadboard-plan.fzz). Die Fritzing-Datei braucht die eigenen
Bauteile (ESP32-C3-DevKitM-1, Micro-SD-Modul, Bodenfeuchtesensor) in der lokalen
Fritzing-Bibliothek.

## Bodenfeuchtesensor (Capacitive Soil Moisture Sensor v1.2)

| Sensor | ESP32-C3 |
|--------|----------|
| VCC    | 3V3      |
| GND    | GND      |
| AOUT   | IO0 (ADC1 Kanal 0) |

## Micro-SD-Kartenmodul (SPI, 3V3-Modul ohne Spannungsregler)

| SD-Modul | ESP32-C3 |
|----------|----------|
| 3V3      | 3V3 (**nicht** 5 V) |
| GND      | GND      |
| CS       | IO7      |
| MOSI     | IO5      |
| CLK      | IO4      |
| MISO     | IO6      |

Die Stiftleiste des Moduls muss verlötet sein, nur durchgesteckt hat sie keinen
zuverlässigen Kontakt. Die Karte muss FAT32-formatiert sein.

## Ton an eine aktive Box (AUX)

Der ESP gibt den Ton als 16-Bit-Audio über die I2S-PDM-Einheit auf IO3 aus (ein schneller
1-Bit-Datenstrom). Die Box verstärkt selbst, die Lautstärke wird an der Box eingestellt.

```
IO3 ──[ 100 Ω ]──────── AUX-Kabel rot (Signal)
GND ─────────────────── AUX-Kabel schwarz (Masse)
```

Auf dem Steckbrett (ESP in Reihe 49–63):

- braunes Kabel von Reihe 53, Spalte A (IO3) nach Reihe 47, Spalte A
- 100-Ω-Widerstand in Spalte B von Reihe 47 nach Reihe 43 (schützt den Pin)
- AUX-Kabel der Box: **rot (Signal) → Reihe 43, Spalte A**, **schwarz (Masse) → untere
  GND-Schiene** (blau)

Optional, falls es rauscht, fiept oder beim Einschalten laut ploppt: ein RC-Tiefpass
(100 nF, Aufdruck „104“, von der Signalseite nach GND) und ein Elko (1–1000 µF, Plus zur
ESP-Seite) in die rote Leitung. Mit der aktuellen Box klingt es ohne beides einwandfrei.

Brummt es, ist das meist eine Masseschleife (Box am Netzteil, ESP am Laptop): Box oder ESP
dann testweise über Akku/Powerbank versorgen.
