# ESP8266 RJ45 Sensor Board

An ESP8266-powered sensor hub with **five RJ45 ports** for **DS18B20 temperature sensors**, plus an I2C connector for **BMP / BME280 / SHT31** sensors. Your sensors connect with plain network cables, so they can live in the next room, in the greenhouse, or inside the beehive. The bees get the cables, you get the couch.

<p align="center">
  <img src="Images/board_with_cable.jpg" width="600" alt="ESP8266 RJ45 Sensor Board">
</p>

Why RJ45? Because Ethernet cables are cheap, sturdy, come in every length imaginable, and you probably already have a drawer full of them. Finally, a use for that drawer.

## MAIN FEATURES :

- **ESP-12F (ESP8266)** – Wi-Fi built in, so the board knows what time it is (NTP) and can send your temperatures wherever you want.
- **5x RJ45 ports** for DS18B20 temperature sensors, with built-in port LEDs.
- **I2C connector** for BME280 / SHT31 sensors (auto-detected at boot) and the OLED display.
- **1.3" SH1106 OLED** (128x64, I2C) with a full menu: overview, per-sensor screens with min/max and trend arrows, ambient readings, alarms, settings and info.
- **Two buttons (A / B)** for navigating the menu, each with an enable jumper (IO12 / IO14) so you can free up the pins if you need them for something else.
- **Buzzer** on IO13, for key beeps, a startup sound and temperature alarms.
- **5-position DIP switch** and a **3.3V / 5V selection jumper** for configuring the sensor side.
- **12V input** through a DC jack or a screw terminal, with reverse-polarity diodes and a resettable fuse.
- **AMS1117-5.0 + AMS1117-3.3** regulators, so everything gets the voltage it likes.
- **Extra connectors**: SPI, 2-pin and 4-pin XH headers, plus a 6-pin programming header.
- **3D-printable case** with a display window, button and port labels.

<p align="center">
  <img src="Images/board_closeup.jpg" width="49%" alt="Board close-up">
  <img src="Images/board_leds_on.jpg" width="49%" alt="Board powered on, port LEDs lit">
</p>

## IMPORTANT INFORMATIONS ! 

1. **Power it from 12V** (DC jack or screw terminal). The onboard regulators take it from there. The 5V regulator is a linear AMS1117, so it gets warm from 12V; that's normal, it just means it's working hard so you don't have to.

2. **There's no USB on this board.** To flash the ESP-12F you need a **USB-to-serial adapter** on the 6-pin programming header. A great match: my [USB-C CH340K Auto-Reset Programmer](https://github.com/mariusmym/USB-C-CH340K-Auto-Reset-Programmer).

3. **GPIO0 is also the 1-Wire bus** for the DS18B20 sensors. GPIO0 decides whether the ESP boots normally or enters flashing mode, so **if uploading fails, unplug the RJ45 sensors** and try again.

4. **The sensors are numbered by the 1-Wire bus, not by the port.** All DS18B20s share the same bus, so "T1" is the first sensor the bus finds (sorted by its unique address), which isn't necessarily the one on port 1. Plug them in one at a time the first time to figure out who's who, then label your cables. Future you will be grateful.

5. **Put your own Wi-Fi name and password** in `secrets.h` (`YOUR_WIFI_SSID` / `YOUR_WIFI_PASS`) before uploading. They live in a separate file so you can share the sketch without sharing your Wi-Fi with the whole internet.

## The firmware 

The sketch in the **SKETCH** folder (**v4.0**) turns the board into a proper little temperature station:

- **Non-blocking readings** – the buttons respond instantly, even while the sensors are busy.
- **Hot-plug** – the 1-Wire bus is rescanned every 30 s, so you can add or swap sensors without restarting. Each sensor is tracked by its unique ROM address.
- **Min / max and a trend arrow** for every sensor (the arrow shows up when the temperature moves more than 0.2 °C per minute).
- **Temperature alarms** for each sensor: HIGH and LOW limits, plus a "sensor lost" alarm when a cable gets unplugged (or chewed). The display wakes up, the buzzer goes beep-beep-beep, and any button silences it. The alarm re-arms on its own once the temperature is back to normal (with 0.5 °C hysteresis, so it doesn't nag you about every tenth of a degree).
- **Settings and alarms are saved in flash**, so they survive a power cut.
- **Automatic clock** – the time syncs over Wi-Fi with automatic summer/winter time. It's set for Romania; change `TZ_INFO` in the sketch for your time zone.
- **Screen-off timer**, adjustable contrast, and a big check mark every time you save something. Everyone deserves a little validation.

### Menu

| Menu item | What it shows |
|---|---|
| **Overview** | All five sensors (plus the ambient sensor) on one screen; sensors in alarm blink |
| **Sensor T1 … T5** | Big temperature, trend arrow, min / max |
| **Ambient** | Temperature + humidity (SHT31) or temperature + humidity + pressure (BME280). Only appears when one of them is connected |
| **Alarms** | Per sensor: alarm ON/OFF, HIGH limit, LOW limit |
| **Settings** | Key beep, alarm sound, screen off (never / 1 / 5 / 15 / 30 min), contrast |
| **Info** | IP address, Wi-Fi signal and channel, uptime, NTP status, sensor count, firmware version |

### Controls

**A** is the left button, **B** is the right one. Tap = short press, hold = long press.

| Where | A | B |
|---|---|---|
| **Menu** | tap: next item · hold: previous | tap: open |
| **Viewing a screen** | tap: next screen · hold: previous | tap: back to menu · hold: reset min/max (on Overview: all sensors) |
| **Alarms / Settings** | tap: + / toggle · hold: − (repeats, speeds up after ~2 s) | tap: next field · hold: **SAVE** & exit |
| **Alarm ringing** | any button: acknowledge | any button: acknowledge |
| **Screen off** | any button wakes it up | any button wakes it up |

Leave an edit screen alone for 60 seconds and it exits without saving. The readings are also printed to the serial monitor (115200 baud) every 10 seconds.

### Libraries

Install these from the Arduino Library Manager:

- **U8g2** (OLED)
- **OneWire** and **DallasTemperature** (DS18B20)
- **Adafruit SHT31**
- **Adafruit BME280** + **Adafruit Unified Sensor** + **Adafruit BusIO**

The time comes from the ESP8266's built-in SNTP, so no extra time libraries are needed.

Board: **Generic ESP8266 Module** or **NodeMCU 1.0 (ESP-12E Module)** from the ESP8266 board package.

### Pinout (from the sketch)

| Function | GPIO |
|---|---|
| DS18B20 1-Wire bus | 0 |
| Button A | 12 |
| Button B | 14 |
| Buzzer | 13 |
| I2C (OLED, sensors) | SDA 4 / SCL 5 |

### Make it yours 

Want MQTT, Home Assistant, a web page, or a push notification when the greenhouse gets too cold? Upload the sketch to any AI tool, tell it what you want. It'll hand you back the updated code (hopefully).

## Main components 

| Part | Component | LCSC |
|---|---|---|
| MCU | ESP-12F (ESP8266) | C89297 |
| RJ45 jacks (x5) | RCH RC02115 | C708657 |
| 5V regulator | AMS1117-5.0 | C6187 |
| 3.3V regulator | AMS1117-3.3 | C2688239 |
| Resettable fuse | Bourns MF-NSMF050-2 | C75464 |
| DIP switch | XKB DS-05BLP | C692503 |
| DC jack | XKB DC-005I-5A-2.5 | C2689704 |
| Screw terminal | KF301-5.0-2P | C474881 |
| Buttons (A, B, Reset) | ST-1188 | C589212 |

Full BOM in the **GERBER, BOM, PNP** folder.

**Not on LCSC, you'll need:**
- **DS18B20 sensors** (the waterproof probe version is perfect), wired to RJ45 plugs or keystone jacks
- **1.3" SH1106 I2C OLED** (128x64)
- **Optional:** BMP280 / BME280 / SHT31 I2C sensor
- A **12V power supply** and some **Ethernet cables** (check the drawer)

## 3D-printable case 

The case is on Printables: https://www.printables.com/model/1869994-esp8266-rj45-sensor-board-case

It has a window for the display, labels for the A / B buttons, numbers for the five ports, and holes for the buzzer, so it can beep at you without being muffled.

<p align="center">
  <img src="Images/case_top.jpg" width="49%" alt="Case, top">
  <img src="Images/case_bottom.jpg" width="49%" alt="Case, bottom">
</p>

## Repository content 

- **GERBER, BOM, PNP** – everything needed to order the PCB from JLCPCB or your favorite fab.
- **SCHEMATIC** – the schematic in PDF.
- **SKETCH** – the Arduino sketch (v4.0), `secrets.h` (your Wi-Fi credentials) and `graphic.h` (the 64x64 check mark).
- **Images** – photos.

![Board with port LEDs](Images/board_leds_on_wide.jpg)

## License 

[![License: CC BY-SA 4.0](https://img.shields.io/badge/License-CC%20BY--SA%204.0-lightgrey.svg)](https://creativecommons.org/licenses/by-sa/4.0/)

This project is licensed under [Creative Commons Attribution-ShareAlike 4.0 International](https://creativecommons.org/licenses/by-sa/4.0/).

- ✅ **Share** – copy and redistribute it in any medium or format
- ✅ **Adapt** – remix, transform, and build upon it
- 🏷️ **Attribution** – give credit and link back here
- 🔁 **ShareAlike** – if you remix it, share your version under the same license

## Donate ☕

If you'd like to say thanks or buy me a coffee, a **[PayPal donation](https://www.paypal.com/donate/?hosted_button_id=KHR7DYJP2Z8QJ)** is always appreciated!

Have fun and enjoy it ! 😊
