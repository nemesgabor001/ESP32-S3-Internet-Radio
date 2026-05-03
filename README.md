# 📻 ESP32-S3 Internet Radio (N16R8)

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![ESP32-S3](https://img.shields.io/badge/Hardware-ESP32--S3-orange)](https://www.espressif.com/en/products/socs/esp32-s3)

A high-performance, feature-rich internet radio based on the ESP32-S3, featuring a customized Skin Engine, real-time audio visualization, and a smart stereo DAC configuration.

---

## 📸 Gallery

<p align="center">
  <img src="Hardware_main.jpg" width="45%" alt="Hardware View">
  <img src="Web_interface.jpg" width="45%" alt="Web Interface">
</p>
<p align="center">
  <img src="Skin_engine.jpg" width="30%" alt="Skin Engine">
  <img src="Station_manager.jpg" width="30%" alt="Station Manager">
  <img src="Build_view.jpg" width="30%" alt="Build View">
</p>

---

## ✨ Features

* **Audio Engine:** Powered by the `ESP32-audioI2S` library for seamless MP3/AAC/OGG streaming.[cite: 2]
* **Stereo Output:** Innovative dual-DAC setup (2x MAX98357A) using a **100k Ohm resistor trick** for true stereo separation without extra GPIOs.[cite: 2]
* **Visuals:**
    * 320x240 TFT display with `LovyanGFX` for ultra-smooth UI.[cite: 2]
    * Real-time Spectrum Analyzer and Analog VU meter.[cite: 2]
* **Customization:** Integrated **Skin Engine** with a WebGUI for real-time color adjustments (Hex-to-RGB).[cite: 2]
* **Smart Features:**
    * **M3U Playlist Parser:** On-the-fly parsing of uploaded or linked playlist files.[cite: 2]
    * **Progressive Alarm:** Fade-in alarm clock with station playback.[cite: 2]
    * **Auto-Dimmer:** Screen dims automatically after 30s of inactivity to protect the display.[cite: 2]
    * **Web Management:** Full-featured web interface for Wi-Fi, stations, skin settings, and OTA updates.[cite: 2]

---

### 🛠️ Hardware Specifications

| Component | Specification |
| :--- | :--- |
| **MCU** | ESP32-S3-WROOM-1 (N16R8 - 16MB Flash / 8MB PSRAM)[cite: 2] |
| **Display** | 320x240 SPI TFT (ST7789 or IL9341)[cite: 2] |
| **Audio** | 2x MAX98357A I2S DAC & Amplifier (Stereo Mod)[cite: 2] |
| **Input** | Rotary Encoder + 1x System Button (K0)[cite: 2] |

### 📌 GPIO Assignment

| Peripheral        | Signal           | ESP32-S3 GPIO                  |
| :---              | :---             | :---                           |
| **Display (SPI)** | MOSI / SCK       | **GPIO 11 / 12**[cite: 2]      |
|                   | CS / DC / RST    | **GPIO 8 / 9 / 10**[cite: 2]   |
|                   | BL (Backlight)   | **GPIO 1**[cite: 2]            |
| **I2S DAC**       | BCLK / DIN / LRC | **GPIO 15 / 16 / 17**[cite: 2] |
| **Encoder**       | CLK / DT / SW    | **GPIO 4 / 5 / 7**[cite: 2]    |
| **System**        | K0 Button        | **GPIO 6**[cite: 2]            |

### 🧠 The 100k Ohm Stereo Trick
To achieve stereo sound with the MAX98357A without using additional pins:
* **Left Channel:** SD pin left floating or tied to VCC.[cite: 2]
* **Right Channel:** Connect a **100k Ohm resistor** between the SD pin and GND.[cite: 2]
* **Wiring:** LRC, BCLK, and DIN lines are shared between both DAC modules.[cite: 2]

---
### 🕹️ Controls & Setup

### Wireless Setup (AP Mode)
1. Connect to Wi-Fi: `Radio_Setup`[cite: 2].
2. Open browser: `192.168.4.1`[cite: 2].
3. Configure your local SSID and Password[cite: 2].

### Physical Buttons
* **K0 Long Press (1.5s):** Enter **Deep Sleep** mode[cite: 2].
* **K0 Extra Long Press (5s):** **Factory Reset**[cite: 2].

---

### 🚀 Build Settings (Arduino IDE)
* **Board:** `ESP32S3 Dev Module`[cite: 2]
* **PSRAM:** `OPI PSRAM`[cite: 2]
* **Partition Scheme:** `16M Flash (3MB APP / 9.9MB FATFS)`[cite: 2]

---

### 🖥️ Web Interface
The built-in web server allows you to:
1. **Manage Stations:** Import M3U playlists or edit 10 presets manually.[cite: 2]
2. **Skin Engine:** Change UI colors (Header, Text, Accent, Background) with a live preview.[cite: 2]
3. **Control:** Adjust volume, set alarms, and switch between visualization modes (Spectrum/VU/Both).[cite: 2]

---

### 🤝 Acknowledgments

This project was developed through the creative collaboration of **Gemini AI** and a human developer. The complex software logic – including the Skin Engine, the M3U parser, and the dual-core task management – was refined and optimized through iterative testing to ensure a professional and stable experience.[cite: 2]

---

### ⚠️ Disclaimer
Use this project with caution. The "100k Ohm Stereo Trick" involves hardware modification of the I2S DAC modules. Ensure your wiring is correct before powering the device.[cite: 1]

---

## 📝 License
**MIT License** - Feel free to use it, improve it, and share it![cite: 1]
