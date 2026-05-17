# Embedded IoT Smart Systems — ESP32 Simon-Says Memory Game

![ESP32](https://img.shields.io/badge/ESP32-000000?style=for-the-badge&logo=espressif&logoColor=white)
![Arduino](https://img.shields.io/badge/Arduino-00979D?style=for-the-badge&logo=arduino&logoColor=white)
![C++](https://img.shields.io/badge/C++-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)
![FreeRTOS](https://img.shields.io/badge/FreeRTOS-3D8B3D?style=for-the-badge)
![Blynk_IoT](https://img.shields.io/badge/Blynk_IoT-2A5DB0?style=for-the-badge)
![I2C](https://img.shields.io/badge/I2C-FF6F00?style=for-the-badge)
![Wi--Fi](https://img.shields.io/badge/Wi--Fi-1BA0F2?style=for-the-badge)

> Full-stack IoT firmware for an ESP32 microcontroller — pairs **FreeRTOS task scheduling** with **Blynk cloud control** and an **I2C-driven LCD** to deliver a smartphone-controlled, 20-level memory-pattern game running on real hardware.

---

## Architecture & Key Features

| Layer | Implementation |
| --- | --- |
| **MCU runtime** | ESP32 DevKit running the Arduino-ESP32 core; FreeRTOS pinned tasks coexist with the Blynk event loop without blocking. |
| **Task scheduling** | A dedicated **FreeRTOS** `GameTask` advances the level-by-level pattern playback and scoring state machine while the main loop services Blynk callbacks and LCD refreshes — no `delay()` calls anywhere in the critical path. |
| **Cloud control** | **Blynk IoT** virtual pins (V0, V2-V5) expose Start + four LED-input buttons that the user taps on a phone dashboard; `BLYNK_WRITE(VPIN_x)` handlers mutate game state safely. |
| **Hardware interface** | **I2C LCD (16x2, address 0x27)** on SDA = GPIO 16, SCL = GPIO 4 renders level number, score, total mistakes, and game-over messages. Four LEDs on GPIO 19/18/5/17 display the pattern. |
| **Game-state machine** | Three explicit phases (pattern-generation, pattern-display, player-matching) tracked by `isShowingPattern` / `isGameOver` flags and a `currentStep` pointer; up to 20 levels with per-level (3) and total (10) mistake quotas. |
| **Security posture** | All Blynk auth tokens and Wi-Fi credentials are placeholder strings (`YOUR_*`) in the published source; original development credentials have been rotated. |

### Repository Layout

```
.
├── README.md
├── .gitignore
├── src/
│   └── esp32_simon_says_memory_game.ino   # Arduino sketch (~460 lines)
```

### Build & Flash

1. Install **Arduino IDE 2.x** with ESP32 board support (`https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`).
2. Install the Arduino libraries **Blynk** (Volodymyr Shymanskyy) and **LiquidCrystal I2C** (Frank de Brabander).
3. Provision your own `BLYNK_TEMPLATE_ID`, `BLYNK_TEMPLATE_NAME`, `BLYNK_AUTH_TOKEN`, `ssid`, and `password` in `src/esp32_simon_says_memory_game.ino`.
4. Select **ESP32 Dev Module**, choose the correct COM port, and click **Upload**.

---

## Co-Author Consent

This firmware was co-authored at Birzeit University with **Jana Herzallah** (1201139) and **Lana Badwan** (1200071). It is published here with the explicit consent of both co-authors.
