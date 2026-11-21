# Sounds

Die Pflanze spielt bei trockener Erde den nächsten Sound einer Shuffle-Playlist. Dafür
werden alle `.wav`-Dateien im **Hauptverzeichnis** der SD-Karte verwendet (Unterordner werden
nicht durchsucht, versteckte Dateien wie `._name.wav` von macOS werden ignoriert).

**Reihenfolge („Smart Shuffle“ wie bei iTunes/Spotify):**

- Jede Datei kommt pro Runde genau einmal dran. Ist die Runde durch, wird die Karte neu
  eingelesen (neue oder gelöschte Dateien zählen ab dann) und neu gemischt.
- Sounds derselben Person werden über die Runde verteilt statt gehäuft. Die Person ist der
  Dateiname ohne Endziffern, z. B. `dom1`–`dom4` → „dom“, `adrian2_2` → „adrian“. Dieselbe
  Person kommt nie direkt hintereinander, sofern andere Sounds da sind.
- Eine neue Runde beginnt nie mit dem zuletzt gespielten Sound und möglichst nicht mit
  derselben Person.
- Der Zufall wird beim Start einmal aus dem Rauschen des ADC gewonnen (der Hardware-
  Zufallsgenerator liefert ohne WLAN/Bluetooth nur Pseudozufall). Dadurch ist die Reihenfolge
  nach jedem Neustart anders. Die Reihenfolge jeder Runde steht im Log.

Liegen keine (gültigen) Dateien auf der Karte, spielt die Pflanze eine eingebaute Sirene.
Nicht abspielbare Dateien werden übersprungen.

## Format

WAV mit PCM, 8 oder 16 Bit, mono oder stereo, 4–48 kHz. MP3/OGG werden nicht
unterstützt (der ESP32-C3 hat keine FPU). Die Wiedergabe läuft mit 16 Bit über I2S an die
AUX-Box, die Dateien werden also in voller Qualität ausgegeben.

Empfohlen: **mono, 16 Bit, Original-Abtastrate** (bis 48 kHz). So wird nichts
heruntergerechnet und keine Qualität verschenkt.

Umwandeln mit [ffmpeg](https://ffmpeg.org) (z. B. `brew install ffmpeg`):

```sh
ffmpeg -i schrei.m4a -ac 1 -af "aresample=osf=s16:dither_method=triangular" -c:a pcm_s16le schrei.wav
```

Alle Aufnahmen aus `resources/m4a/` nach `resources/wav/` (Leerzeichen im Namen werden zu `_`):

```sh
cd resources
for f in m4a/*.m4a m4a/*.mp3; do
  b=$(basename "$f"); b="${b%.*}"
  ffmpeg -y -i "$f" -ac 1 -af "aresample=osf=s16:dither_method=triangular" -c:a pcm_s16le "wav/${b// /_}.wav"
done
```

Die fertigen WAVs direkt ins Hauptverzeichnis der SD-Karte kopieren.

Alternativ in Audacity: *Spur → Stereo nach Mono*, Projekt-Rate unverändert lassen
(höchstens 48000 Hz), dann *Exportieren → WAV, Signed 16-bit PCM*.

## Einstellungen (`idf.py menuconfig`)

| Menü | Option | Standard |
|------|--------|----------|
| Screaming Plant: Soil moisture sensor | Scream when moisture is at or below (%) | 30 |
| Screaming Plant: Soil moisture sensor | Minimum time between two screams (s) | 60 |
| Screaming Plant: Audio | Volume (%) — 100 = unverändert, laut stellt man an der Box | 100 |
