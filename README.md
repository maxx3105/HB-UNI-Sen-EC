# HB-UNI-Sen-EC

universeller HomeMatic EC-Sensor (Electrical Conductivity / Elektrische Leitfähigkeit)

basierend auf [HB-UNI-Sen-PH-ORP](https://github.com/jp112sdl/HB-UNI-Sen-PH-ORP) von jp112sdl

## Bauteile

- Arduino Pro Mini (3.3V / 8MHz)
- CC1101 868MHz Funkmodul
- EC Sensor (K=1 **oder** K=10, siehe Varianten)
- I2C LCD Display 2x16
- DS18B20 Temperatursensor (wasserdicht)
- Widerstände: 330 Ohm (LED), 4,7k (DS18B20 Pull-up)

## Sensor-Varianten

| Define im Sketch | Sensor | SKU | Messbereich | Einsatzgebiet | Kalibrierung |
|---|---|---|---|---|---|
| `EC_SENSOR_K1` | DFRobot EC V2 | DFR0300 | 0 – 20 mS/cm | Trinkwasser, Pool, Aquaponik | Two-Point (1413 µS/cm + 12.88 mS/cm) |
| `EC_SENSOR_K10` | DFRobot EC K=10 | DFR0300-H | 10 – 100 mS/cm | Salzwasser, Meerwasser | Single-Point (12.88 mS/cm) |

## Pinbelegung (Arduino Pro Mini 3.3V)

| Pin | Funktion |
|---|---|
| 13 / SCK | CC1101 SCK |
| 12 / MISO | CC1101 MISO |
| 11 / MOSI | CC1101 MOSI |
| 10 / CS | CC1101 CS |
| 2 / INT0 | CC1101 GDO0 |
| 8 | Config-Button (gegen GND, INPUT_PULLUP) |
| 4 | LED (+330Ω gegen GND) |
| 5 | DS18B20 Data (+4,7kΩ Pull-up gegen VCC) |
| A1 | EC-Sensor analoger Ausgang |
| A4 / SDA | I2C LCD SDA |
| A5 / SCL | I2C LCD SCL |

## Abhängigkeiten

In der Arduino IDE installieren:

1. [AskSinPP](https://github.com/pa-pa/AskSinPP) (master-Branch!)
2. EnableInterrupt (Bibliotheksmanager)
3. Low-Power (Bibliotheksmanager)
4. OneWire (Bibliotheksmanager)
5. LiquidCrystal_I2C (Bibliotheksmanager)
6. EC Library je nach Sensor:
   - K=1: [DFRobot_EC](https://github.com/DFRobot/DFRobot_EC)
   - K=10: [DFRobot_EC10](https://github.com/DFRobot/DFRobot_EC10)

## Board-Einstellungen

- Board: Arduino Pro or Pro Mini
- Prozessor: ATmega328P (3.3V, 8 MHz)

## Konfiguration

Im Sketch `HB-UNI-Sen-EC.ino` anpassen:

```cpp
// Sensor-Typ (genau eine Zeile einkommentieren):
#define EC_SENSOR_K1          // K=1  (0-20  mS/cm)
//#define EC_SENSOR_K10       // K=10 (10-100 mS/cm)

// Device-ID und Serial (muss unique sein!):
{ 0xFC, 0x20, 0x01 },   // Device ID
"JPEC000001",            // Serial
```

## CCU Einstellungen

### Geräte-Parameter (über WebUI konfigurierbar)

| Parameter | Beschreibung | Default |
|---|---|---|
| HB_MEASURE_INTERVAL | Messintervall in Sekunden | 10 s |
| HBWEA_TRANSMIT_INTERVAL | Senden nach N Messungen | 18 |
| BACKLIGHT_ON_TIME | LCD Hintergrundbeleuchtung Timeout | 10 s |
| TEMPERATURE_OFFSET | Temperatur-Offset (-3.5K .. +3.5K) | 0.0K |

### Datenpunkte (Kanal 1)

| Datenpunkt | Einheit | Beschreibung |
|---|---|---|
| `TEMPERATURE` | °C | Wassertemperatur |
| `HB_EC` | mS/cm | Elektrische Leitfähigkeit |

## Kalibrierung

**Kalibrierungsmodus starten:** Config-Button lang drücken (3 Sekunden)

### K=10 (Single-Point)

| Schritt | LCD-Anzeige | Aktion |
|---|---|---|
| 1 | `EC CALIBRATION` | Button kurz drücken |
| 2 | `Put 12.88ms/cm` | Sonde in 12.88 mS/cm Lösung, dann Button drücken |
| 3 | `Done. Saved.` | Fertig, K-Wert im EEPROM gespeichert |

### K=1 (Two-Point)

| Schritt | LCD-Anzeige | Aktion |
|---|---|---|
| 1 | `EC CALIBRATION` | Button kurz drücken |
| 2 | `Put 1413us/cm` | Sonde in 1413 µS/cm Lösung, dann Button drücken |
| 3 | `1413us READ OK` | Button kurz drücken |
| 4 | `Put 12.88ms/cm` | Sonde in 12.88 mS/cm Lösung, dann Button drücken |
| 5 | `Done. Saved.` | Fertig, beide K-Werte im EEPROM gespeichert |

### Fehlerbehandlung

Falls die gemessene Spannung nicht zum erwarteten Kalibrierlösungsbereich passt, zeigt das Display einen spezifischen Fehler an und springt zum vorherigen Schritt zurück:

| Fehlermeldung | Ursache |
|---|---|
| `ERR: no signal` | Sonde nicht angeschlossen oder kein Kontakt |
| `ERR: not 1413us` | Spannung außerhalb des 1413 µS/cm Bereichs |
| `ERR:not 12.88ms` | Spannung außerhalb des 12.88 mS/cm Bereichs |

### Serielle Kalibrierung

Alternativ kann über den seriellen Monitor (57600 Baud) kalibriert werden:

| Befehl | Funktion |
|---|---|
| `enterec` | Kalibrierungsmodus starten |
| `calec` | Kalibrierung mit aktueller Lösung durchführen |
| `exitec` | Kalibrierparameter im EEPROM speichern & Modus beenden |

## Button-Funktionen

| Aktion | Funktion |
|---|---|
| Kurz drücken | Pairing starten (oder nächster Kalibrierschritt im Kalibriermodus) |
| Kurz drücken | LCD Hintergrundbeleuchtung einschalten |
| Lang drücken (3s) | Kalibrierungsmodus ein/aus |
| 2x Lang drücken | Geräte-Reset |

## HomeMatic Addon (XML)

Die Datei `hb-uni-sen-ec.xml` muss als Gerätebeschreibung in die CCU eingespielt werden.

Bei Verwendung des [JP-HB-Devices Addon](https://github.com/jp112sdl/JP-HB-Devices-addon) kann die XML-Datei im Addon-Verzeichnis abgelegt werden.

Device Model: **0xFC20**

## Lizenz

Creative Commons - http://creativecommons.org/licenses/by-nc-sa/3.0/de/
