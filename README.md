# ⚡ PC-Remote-Switch — ESP32 Wi-Fi & mDNS Remote Power Controller

A lightweight, robust embedded firmware project built with **PlatformIO** for the ESP32 / ESP8266 microcontroller. Allows you to remotely turn on, force shutdown, reset, or monitor your desktop PC over your local Wi-Fi network with mDNS discovery and PWM vibration feedback.

---

## 🚀 Features

- **🌐 Embedded Web Server**: Responsive web dashboard accessible from any browser on your local network (e.g. `http://goodesp.local`).
- **🔍 mDNS Hostname Discovery**: Automatically broadcasts `goodesp.local`—no need to look up changing local router IP addresses.
- **⚡ Motherboard Pin Triggering**: Accurately pulses ATX power and reset headers via optocouplers or relay modules (short press for power on/sleep, 5-second long press for forced hard reset).
- **📳 PWM Vibration Feedback**: Drives a haptic vibration motor using ESP32 PWM channels (`ledc`) to provide physical confirmation when actions are executed.
- **💾 Flash Memory Storage**: Uses ESP32 `Preferences` non-volatile storage to persist device state and network configurations across reboots.

---

## 🛠️ Hardware Requirements

* **Microcontroller**: ESP32 DevKit v1 (or NodeMCU ESP8266).
* **Motherboard Interface**: 2x Optocouplers (e.g. PC817) or 5V relay module connected to PC Front Panel `POWER SW` and `RESET SW` pins.
* **Haptic Feedback (Optional)**: Small 3V/5V DC vibration motor driven by an NPN transistor (e.g. 2N2222) on GPIO 18.

---

## 💻 Quick Start & Flashing

### 1. Prerequisites
Install [PlatformIO](https://platformio.org/) (via VS Code or CLI).

### 2. Wi-Fi Configuration
Copy the template header to `include/wifi_config.h`:
```bash
cp include/wifi_config.example.h include/wifi_config.h
```
Edit `include/wifi_config.h` with your local Wi-Fi credentials:
```c
#pragma once
#define WIFI_CONFIG_HAS_CREDENTIALS 1
static const char WIFI_CONFIG_SSID[] = "YOUR_WIFI_SSID";
static const char WIFI_CONFIG_PASSWORD[] = "YOUR_WIFI_PASSWORD";
```

### 3. Build & Flash
Connect your ESP32 via USB and run:
```bash
pio run --target upload
```

### 4. Usage
Open any browser on your home Wi-Fi and navigate to:
```text
http://goodesp.local
```

---

## 📜 License
MIT License.
