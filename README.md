# Find all files and 3D Model here:
https://makerworld.com/de/models/2254346-1d-rgb-invader-retro-game#profileId-2455425


# 1D-RGB-Invader
ESP32 powered WS2812B 1D Space Invader inspired game
https://youtube.com/shorts/Ps4TY1oXYPk?si=DmJbKW9S6Qn9KOkK

https://github.com/user-attachments/assets/9a7cbed4-157b-47ac-b319-e42f20a55934

# 👾 Ultimate 1D RGB Invaders 👾  
ESP32-powered WS2812B 1D arcade shooter inspired by Space Invaders

Turn a LED strip into a playable arcade game.  
One pixel is your ship. The rest is hostile.

---

## ⚠️ Project Status & Version Recommendation (READ FIRST)

This project exists in **two hardware variants**:

### ✅ Recommended & Actively Developed
**Sound Edition – ESP32-S3**
- Full arcade sound via I2S amplifier  
- Web UI, OTA, highscores, bosses  
- Actively developed and extended  
- **This is the version you should build**

### ⚠️ Legacy / Maintenance Only
**Silent Edition – ESP32-S2**
- No sound  
- Fully functional  
- **No new features planned**
- Kept only for compatibility

➡️ **All main documentation below refers to the ESP32-S3 Sound Edition.**  
➡️ ESP32-S2 instructions are collected **at the very end** to avoid confusion.

---

## 🎮 Features (ESP32-S3 Sound Edition)

- Old-school arcade sound (I2S)
- Boss fights
- Highscore system
- Web configuration interface
- OTA firmware updates
- Adjustable difficulty
- Preset kids mode
- Single `.ino` file (Arduino)

---

## ☠️ POWER & SAFETY – DO NOT SKIP ☠️

🚫 **NEVER power the LED strip via the ESP32 USB port**  
WS2812B strips can draw **several amps**.  
The ESP32 cannot supply this.

🔥 **DO NOT connect the ESP32 to a PC via USB while the LEDs are externally powered**

**Correct workflow:**
- Flashing firmware → **NO external LED power**
- Playing the game → **NO PC USB connection**

Failure results in:
- Burnt USB ports  
- Dead ESP32  
- Potential PC damage  

---

## 🛠 Hardware – ESP32-S3 Sound Edition (Recommended)

### Core Components

| Part | Description |
|----|----|
| MCU | **ESP32-S3 DevKitC-1 (N16R8 recommended)** |
| LED Strip | WS2812B (ECO), 60 LEDs/m |
| Audio Amp | MAX98357A I2S |
| Speaker | 4 Ω / 3 W |
| Buttons | 3× 60 mm arcade buttons |
| Menu Button | 1× 12 mm momentary |
| Power | USB-C PD trigger (fixed 5 V) |
| PSU | USB-C power supply (≥45 W recommended) |
| Misc | Wires, connectors, 3D printed case |

**Typical LED length:** 2–4 m  
**Estimated total cost:** ~35–50 €

---

## 🔌 Wiring – ESP32-S3 Sound Edition

### General Rules
- **Buttons:** GPIO ↔ GND (internal pullups used)
- **LED Power:** directly from 5 V PSU
- **ESP32 GND and LED GND must be common**
- **LED data line only goes to ESP32**

### Pin Mapping (ESP32-S3)

| Function | GPIO | Notes |
|----|----|----|
| Button Blue | 15 | |
| Button Red | 16 | |
| Button Green | 17 | |
| Menu Button | 18 | Hold for WiFi |
| LED Data | 7 | WS2812B |
| I2S BCLK | 4 | MAX98357A |
| I2S LRC | 5 | MAX98357A |
| I2S DIN | 6 | MAX98357A |
| Amp Power | 5 V / GND | External 5 V |

---

## 💻 Software Setup – ESP32-S3 (Critical)

### Arduino IDE
- Arduino IDE **2.x**
- Library:
  - **FastLED** (Daniel Garcia)

---

### ESP32 Board Package (IMPORTANT)

⚠️ **Audio requires a specific ESP32 core version**

- Board package: `esp32` by Espressif
- **Required version: 2.0.17**
- ❌ Do **NOT** use 3.x

### Board Settings

- Board: **ESP32S3 Dev Module**
- Flash Mode: DIO 80 MHz
- Flash Size: 16 MB
- Partition: 16M (3MB APP / 9.9MB FATFS)
- PSRAM: **Disabled**
- USB CDC on Boot: **Disabled**

---

## 🚀 Uploading Firmware (ESP32-S3)

- Use USB port labeled **UART / COM**
- If upload fails:
  1. Hold **BOOT**
  2. Press **RST**
  3. Release **BOOT**
  4. Upload sketch

---

## 🌐 First Start & Web Configuration

1. Power system using **external 5 V**
2. Hold **menu button** for **4 seconds**
3. LED strip turns **blue**
4. Connect WiFi:
   - SSID: `ESP-RGB-INVADERS`
   - Password: `12345678`
5. Open browser:
   - `http://192.168.4.1`
6. Configure:
   - LED count
   - Brightness
   - Sound options
7. Save → automatic reboot

---

# 🧊 LEGACY SECTION – ESP32-S2 Silent Edition (Deprecated)

⚠️ This section is provided **for reference only**.  
No new features will be added.

## Hardware
- MCU: **LOLIN S2 Mini**
- No audio hardware

## Pin Mapping (S2)

| Function | GPIO |
|----|----|
| Button Blue | 3 |
| Button Red | 5 |
| Button Green | 7 |
| Menu Button | 9 |
| LED Data | 16 |

## Board Settings
- Board: **LOLIN S2 MINI**
- USB CDC On Boot: Enabled
- Upload Mode: Internal USB (OTG)

## Uploading (S2)
1. Hold **Button 0**
2. Press **RST**
3. Release **Button 0**
4. Upload sketch

---

## 📦 Files & 3D Models
https://makerworld.com/de/models/2254346-1d-rgb-invader-retro-game#profileId-2455425

---

## 🎮 Have fun saving the galaxy.
