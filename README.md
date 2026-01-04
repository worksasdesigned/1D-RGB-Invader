# 1D-RGB-Invader
ESP32 powered WS2812B 1D Space Invader inspired game

https://github.com/user-attachments/assets/9a7cbed4-157b-47ac-b319-e42f20a55934

# 👾👾 Ultimate RGB Invaders 👾

**A 1-Dimensional Arcade Shooter on a WS2812B LED Strip powered by ESP32**

Welcome to **Ultimate RGB Invaders**!  
This project transforms a simple LED strip into a high-intensity arcade shooter.

You control a spaceship (a single dot of light) on one end of the strip, shooting down enemies approaching from the other side. The game features sound effects, boss battles, and a full web-based configuration interface.

---

## 🎮 Game Features

- **10 Unique Levels**  
  Progression from simple enemies to the ultimate RGB Overlord.
- **Web Interface**  
  Configure game speed, brightness, difficulty, LED length and Wi-Fi settings directly from your smartphone.
- **Responsive Audio**  
  8-bit retro sound effects for shooting, explosions, and alarms via I2S.

---

## 👹 The Bosses

Prepare yourself for three distinct boss encounters:

### 🧠 Masterblaster (Level 3)
A tactical genius. Uses energy barriers to protect its core.  
**Timing is everything** — blind firing won’t help you here.

### 🛡 The Tank (Level 6)
Heavily armored and relentless. Moves slowly but absorbs damage like a sponge.  
Don’t let it get too close, or it’s game over.

### 🌈 RGB Overlord (Level 10)
The ultimate chaotic force. 
Only players with perfect reflexes will survive the chromatic onslaught.

---

## ⚠️ Critical Power Warning

🚫 **DO NOT power this project solely via the ESP32 USB port!**

- This game can drive **100–300 LEDs** --> 240 are recommended
- Full white brightness can exceed standard USB current limits
- ⚠️ ⚠️ CHECK YOUR wriring ! Do NOT connect ESP32 to your PC while LEDs are connected to ESP32! DO NOT connect ESP32 to your PC while external Power Source is still connected to your setup! ⚠️ ⚠️ 


### ✅ Power Requirements
- **5V high-power supply (minimum 5A recommended)**
- **Recommended:**  
  A recycled **45W+ USB-C laptop charger** combined with a **USB-C PD Trigger Board** (set to 5V)

---

## 📡 Quick Start: Wi-Fi Connection

Once powered on, the game creates its own Wi-Fi hotspot.

1. Connect to the Wi-Fi network:
ESP32-Invader-Ult
Password: 12345678

2. Open your browser and go to:
http://192.168.4.1

(Usually opens automatically)

---

## 🛠 Hardware & Parts List

| Part | Description | Link |
|----|------------|------|
| Microcontroller | ESP32 Lolin S2 Mini | [LINK TO SHOP] (https://www.amazon.com/HiLetgo-ESP32-S2FN4R2-ESP32-S2-Type-C-Connect/dp/B0B291LZ99)](https://de.aliexpress.com/item/1005006828096971.html) |
| Power | USB-C PD Trigger Board (5V configurable) | [LINK TO SHOP] https://de.aliexpress.com/item/1005007010060543.html |
| LED Strip | WS2812B (ECO), 4m, 60 LEDs/m | [LINK TO SHOP] https://de.aliexpress.com/item/1005007889104592.html |
| Buttons | 4× 60mm Arcade Buttons (Red, Green, Blue, White) | [LINK TO SHOP] https://de.aliexpress.com/item/1005008893549021.html|
| Audio Amp | MAX98357A I2S Amplifier | [LINK TO SHOP] https://de.aliexpress.com/item/1005010526780490.html|
| Speaker | 4Ω 3W Speaker | [LINK TO SHOP] https://de.aliexpress.com/item/1005008626624201.html |
| Case | 3D-Printed Case (STL files in `/stl`) | MAKERWORLD |
| Misc | Wires, soldering iron, 45W+ USB-C PSU | EBAY or your Spareparts box :-) I used a 45W USBc Charger from an old Notebook |

ALL LINKS ARE NON Affiliate Links. Best you can do: Buy at your local small Maker-store.
Total Costs ~ 35-40EUR (LED Stripe is pretty expensive if you choose a coated one)

<img width="399" height="279" alt="image" src="https://github.com/user-attachments/assets/9e0846d6-4112-45e3-a35b-c25188706ff3" />

You can choose more or less every ESP32 Board of choice. Just ask ChatGPT which GPIOs you must choose instead.

---

## 🔌 Wiring Guide

### 1️⃣ Power Setup (USB-C PD Board)

⚠️ **Important:** Configure your PD Trigger Board to output **5V**.

- Connect **VBUS (+)** and **GND (−)** from the PD board directly to the LED strip
- Branch VBUS and GND to power the ESP32 (`VBUS` + `GND`) 
- use a WAGO or built a small cable tree.

📷 *Insert power wiring photo here*

---

### 2️⃣ Button Wiring (Lolin S2 Mini)

- One leg of each button → **GND**  -> you can connect all ground legs together.
- Other leg → GPIO pin

| Button Function | Color | ESP32 Pin |
|---------------|------|-----------|
| Shoot Blue | Blue | 3 |
| Shoot Red | Red | 5 |
| Shoot Green | Green | 7 |
| Reset / Menu | White | 9 |

**Wiring Tip:**  
Daisy-chain all GND wires or merge them using a Wago connector.

**Soldering Tip:**  
Twist stranded wires tightly and pre-tin lightly.  
Too much solder = it won’t fit!

📷 *Insert button wiring photo here*

---

### 3️⃣ Audio (MAX98357A)

| MAX98357A Pin | ESP32 Pin |
|-------------|-----------|
| LRC | 12 |
| BCLK | 14 |
| DIN | 18 |
| VIN | 5V (VBUS) |
| GND | GND |

---

### 4️⃣ LED Data

- LED Strip **DI (Data In)** → **ESP32 GPIO 16**

---

## 💻 Software Setup (Arduino IDE)

### 1️⃣ Install Arduino IDE
Download **Arduino IDE 2.3.7 or newer** from  
👉 https://www.arduino.cc

---

### 2️⃣ Install ESP32 Board Support

Simply go to Boardmanager (tools - boards) and install Expressiv ESP32 boards. IF you run into timeout errors while downloading, you might edit the Arduino IDE and edit the config yaml with:
network:
  connection_timeout: 600s    
(google it, you will find the solution for Windows, Mac, Linux)

If you can't find ESP32 Boards:
Add this URL in **File → Preferences → Additional Boards Manager URLs**:
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json


Then:
- Tools → Board → Boards Manager
- Search **esp32**
- Install **Espressif Systems**

---

### 3️⃣ Install Libraries

- **FastLED** (by Daniel Garcia)

---

### 4️⃣ Board Settings (LOLIN S2 MINI)

- Board: `ESP32 Arduino → LOLIN S2 MINI`
- USB CDC On Boot: **Enabled**
- Upload Mode: **Internal USB (OTG)**

---

## 🚀 How to Flash (Upload Code)

copy the code from .ino file over the new created project. 
There are 2 Versions. 
1D_NOsound.ino  ---> no sound , speaker required
1DRGB_invader.ino ---> full version of the game

⚠️ The Lolin S2 Mini can be tricky to enter upload mode.

### Cable Check
Use a **USB-C data cable** — many cheap cables are charge-only!

### Bootloader Mode
1. Hold **button 0**
2. Press **RST** briefly
3. Release **RST**
4. Release **0**

The board should now appear as a COM port.

➡️ Click **Upload** in Arduino IDE  
➡️ After upload, press **RST** once to start the game

---

### First steps
1. CHECK YOUR wriring ! Do NOT connect ESP32 to your PC while LEDs are connected to ESP32! DO NOT connect ESP32 to your PC while external Power Source is still connected to your setup!
2. Connect to Wifi and configure number of LEDs
3. Play the game


## 🐛 Troubleshooting

**“Port not found”**  
→ Boot button combo not done correctly or charge-only cable.

**“Brownout detector was triggered”**  
→ Power supply too weak. Lower brightness or use a stronger PSU.

**LEDs flicker / wrong colors**  
→ Ensure LED strip GND is connected to ESP32 GND.
→ Ensure LED datawire is short. Sometimes a small resistor ~300Ohm will help. or use a single LED VERY close to the data pin. ESP32 provides dataflow with 3.3V WS2812B likes 5V data signal.  


---

## 🚀 Have fun saving the galaxy!



