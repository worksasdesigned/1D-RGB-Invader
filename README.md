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
  Configure game speed, brightness, difficulty, and Wi-Fi settings directly from your smartphone.
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

- This game can drive **100–300 LEDs**
- Full white brightness can exceed standard USB current limits

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
| Microcontroller | ESP32 Lolin S2 Mini | [LINK TO SHOP] |
| Power | USB-C PD Trigger Board (5V configurable) | [LINK TO SHOP] |
| LED Strip | WS2812B (ECO), 4m, 60 LEDs/m | [LINK TO SHOP] |
| Buttons | 4× 60mm Arcade Buttons (Red, Green, Blue, White) | [LINK TO SHOP] |
| Audio Amp | MAX98357A I2S Amplifier | [LINK TO SHOP] |
| Speaker | 4Ω 3W Speaker | [LINK TO SHOP] |
| Case | 3D-Printed Case (STL files in `/stl`) | — |
| Misc | Wires, soldering iron, 45W+ USB-C PSU | — |

---

## 🔌 Wiring Guide

### 1️⃣ Power Setup (USB-C PD Board)

⚠️ **Important:** Configure your PD Trigger Board to output **5V**.

- Connect **VBUS (+)** and **GND (−)** from the PD board directly to the LED strip
- Branch VBUS and GND to power the ESP32 (`VBUS` + `GND`)

📷 *Insert power wiring photo here*

---

### 2️⃣ Button Wiring (Lolin S2 Mini)

- One leg of each button → **GND**
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



