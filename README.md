# Find all files and 3D Model here:
https://makerworld.com/de/models/2254346-1d-rgb-invader-retro-game#profileId-2455425


# 1D-RGB-Invader
ESP32 powered WS2812B 1D Space Invader inspired game
https://youtube.com/shorts/Ps4TY1oXYPk?si=DmJbKW9S6Qn9KOkK

https://github.com/user-attachments/assets/9a7cbed4-157b-47ac-b319-e42f20a55934

# 👾 Ultimate 1D RGB Invaders (v3.2 & v4.2) 👾

**The 1D Arcade Shooter that proves you don't need 4K graphics to sweat.**

Welcome to **Ultimate RGB Invaders**.  
You are about to turn a strip of lights into a battlefield. You control a spaceship (a single, brave pixel) fighting against waves of chromatic enemies.

- Bosses ✔  
- Highscores ✔  
- Web Interface ✔ (because we are living in the future)
- OTA updates ✔
- fully adjustable and preset kids mode 

Simply copy & paste the .ino file into a new Arduino project.

There are **two versions**:
- with oldschool arcade sound (recommended latest Version) req. ESP32 S3
- without sound (V3.2) - running on ESP32 S2

Demo (no sound version):  
*(insert link / media here)*

---

## ☠️ THE "DO NOT EXPLODE" SECTION ☠️  
**READ THIS OR CRY LATER.**

🚫 **NEVER** power this project solely via the ESP32 USB-C port.  
The LEDs are hungry beasts. The ESP32 is a delicate flower. They do not share food.

🔥 **MAGIC SMOKE WARNING**  
Do **NOT** connect the ESP32 to your PC (USB) while the LEDs are powered by an external power supply.

**Scenario:**  
You want to update the code?  
→ Unplug the external power first!  
→ Unplug the LED strip from the board if possible!

**Why?**  
Sending 5 Amps through your PC's motherboard is an excellent way to buy a new PC.

---

## 🛠 Hardware & Parts List

### Choose your Fighter

- **Version 1 (Silent)**  
  The classic. Cheaper, easier to build.  
  Uses **Lolin S2 Mini**.

- **Version 2 (Sound Edition)**  
  Full arcade experience with retro-synth audio.  
  Uses **ESP32-S3**.

---

## ☠️ THE "DO NOT EXPLODE" SECTION ☠️  
*(Yes. Again. Because people skip it.)*

🚫 **NEVER** power this project solely via the ESP32 USB-C port  
(LEDs can draw **up to 5 Amps**).

🔥 **Do NOT** connect the ESP32 to your PC while LEDs are externally powered.

---

## 🧾 Parts & Cost Notes

- All links are **NON-affiliate**
- Best option: support your **local maker store**
- **Estimated total cost:** ~35–50 EUR  
  (LED strips get expensive if you choose coated ones)

---

## 📦 Shared Components (BOTH versions)

| Part | Description | Link (Example) |
|----|----|----|
| Power | USB-C PD Trigger Board (5V configurable) | [AliExpress](https://www.google.com/search?q=https://de.aliexpress.com/item/1005007010060543.html) |
| LED Strip | WS2812B (ECO), 60 LEDs/m (Recommended: ~2–4m) | [AliExpress](https://www.google.com/search?q=https://de.aliexpress.com/item/1005007889104592.html) |
| Buttons | 3× 60mm Arcade Buttons (Red, Green, Blue) | [AliExpress](https://www.google.com/search?q=https://de.aliexpress.com/item/1005008893549021.html) |
| Menu Button | 1× 12mm Momentary Button | [AliExpress](https://www.google.com/search?q=https://de.aliexpress.com/item/1005010368828186.html) |
| PSU | USB-C Charger (45W+ recommended) | Old Laptop Charger |
| Misc | Wires, Hot Glue, Cable Ties, 3D Printed Case | Local Hardware Store |

---

## 🔇 Version 1: Silent Edition (Specific Parts)

| Part | Description | Link |
|----|----|----|
| MCU | ESP32 Lolin S2 Mini | [AliExpress](https://www.google.com/search?q=https://de.aliexpress.com/item/1005006828096971.html) |

---

## 🔊 Version 2: Sound Edition (Specific Parts)

| Part | Description | Link |
|----|----|----|
| MCU | ESP32-S3 DevKitC-1 (N16R8) | [Eckstein Shop](https://www.google.com/search?q=https://eckstein-shop.de/WaveShare-ESP32-S3-Microcontroller-24GHz-Wi-Fi-Development-Board%3Fws_oss_lieferland%3DCH%26srsltid%3DAfmBOooHC5LKcMW4IOEhddoCjtfklhrVoK-3E61sh-TB9_i2gWNpLEyr9v4) |
| Amplifier | MAX98357A I2S Amplifier | [Eckstein Shop](https://eckstein-shop.de/AdafruitI2S3WClassDAmplifierBreakout-MAX98357A) |
| Speaker | 4 Ohm 3W Speaker | [Eckstein Shop](https://eckstein-shop.de/AdafruitMonoEnclosedSpeaker-3W4Ohm) |

---

## 🔌 Wiring & Pin Mapping

### Common Wiring Rules
- **Buttons:** One leg → GPIO, other leg → GND  
- **LEDs:**  
  - Data → GPIO  
  - Power LEDs **directly** from PD Trigger (5V)  
  - **NOT through the ESP32**

---

### 📍 Mapping – Version 1 (Lolin S2 Mini)

| Component | ESP32 Pin | Note |
|----|----|----|
| Button Blue | 3 | |
| Button Red | 5 | |
| Button Green | 7 | |
| Button 12mm Silver | 9 | Menu / WiFi |
| LED Data | 16 | |

---

### 📍 Mapping – Version 2 (ESP32-S3 Sound)

| Component | ESP32-S3 Pin | Note |
|----|----|----|
| Button Blue | 15 | |
| Button Red | 16 | |
| Button Green | 17 | |
| Button 12mm Silver | 18 | Menu / WiFi |
| LED Data | 7 | |
| Audio BCLK | 4 | MAX98357A BCLK |
| Audio LRC | 5 | MAX98357A LRC |
| Audio DIN | 6 | MAX98357A DIN |
| Audio Power | 5V / GND | MAX98357A Vin / GND |

---

## 💻 Software Setup (CRITICAL)

This is where most people fail. Read carefully.

---

### Step 1: Arduino IDE & Libraries
1. Install **Arduino IDE 2.x**
2. `Sketch → Include Library → Manage Libraries`
3. Install **FastLED** (by Daniel Garcia)

---

### Step 2: Board Manager & Settings

#### 👉 Version 1 – Lolin S2 Mini
- Board Manager: `esp32` by Espressif (latest OK)
- Board: **LOLIN S2 MINI**
- Tools Settings:
  - USB CDC On Boot: **Enabled**
  - Upload Mode: **Internal USB (OTG)**

---

#### 👉 Version 2 – ESP32-S3 Sound

⚠️ **IMPORTANT:** Audio requires a specific core version.

- Board Manager: `esp32`
- **DOWNGRADE REQUIRED:**  
  Install **Version 2.0.17**  
  ❌ Do NOT use 3.0.x
- Board: **ESP32S3 Dev Module**
- Tools Settings:
  - Flash Mode: DIO 80MHz
  - Flash Size: 16MB (128Mb)
  - Partition Scheme: 16M Flash (3MB APP / 9.9MB FATFS)
  - PSRAM: **Disabled**
  - USB CDC On Boot: **Disabled**

---

## 🚀 Uploading the Sketch

### Version 1 – S2 Mini
You **must** force Download Mode:
1. Hold **Button 0**
2. Press **RST**
3. Release **Button 0**
4. Upload sketch

---

### Version 2 – ESP32-S3
- Use USB port labeled **UART / COM**
- If upload fails:
  1. Hold **BOOT**
  2. Press **RST**
  3. Release **BOOT**
  4. Upload sketch

---

## 🎮 First Start & Web Configuration

1. Power up using **external power**
2. Enter Menu:  
   Hold **12mm silver button** for **4 seconds**  
   → LED strip turns **BLUE**
3. Connect via WiFi:
   - SSID: `ESP-RGB-INVADERS`
   - Password: `12345678`
4. Open browser:  
   `http://192.168.4.1`
5. Configure:
   - LED count
   - Brightness
   - Sound config (Version 2 only)
6. Click **Save** → ESP restarts

**Good luck & have fun.**



## 🚀 Have fun saving the galaxy!



