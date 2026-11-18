# 🎮 CYD + PS4 — Memory Combo Game

A memory game for the ESP32 **Cheap Yellow Display (CYD)** controlled by a **PS4 DualShock 4** over Bluetooth Classic.

The screen flashes a sequence of controller buttons; repeat it in order. Each round adds one button. Finish a 12-button sequence to win.

| Mode | Buttons used | First round | Flash / gap | Time per button |
| --- | --- | --- | --- | --- |
| Easy | X, O, Square, Triangle | 2 symbols | 750 / 250 ms | None |
| Hard | Face buttons + L1, R1, L2, R2 | 4 symbols | 350 / 180 ms | 5 seconds |

## Hardware
- ESP32 CYD (original ESP32 / `esp32dev`, 4 MB flash) — Bluetooth Classic is required, so S3/C3/C6 boards won't work
- Sony DualShock 4 controller

## Code
- `main/sketch.cpp` — Bluetooth pairing, difficulty menu, playback and input
- `main/memory_game.h` — game state machine
- `main/cyd_display.cpp` — CYD screen driver
- `tests/memory_game_test.cpp` — behavior tests for the game logic

## Build
Built with PlatformIO, ESP-IDF 5.4.2, Arduino-ESP32 3.2.1 and [Bluepad32](https://github.com/ricardoquesada/bluepad32).

1. Fetch the Arduino core: `git clone --depth 1 https://github.com/espressif/arduino-esp32.git components/arduino`
2. Set `build_dir` in `platformio.ini` to a path **without spaces** (ESP-IDF requirement), or delete that line.
3. Open the folder in VS Code with PlatformIO → Build → Upload → Monitor at 115200 baud.
4. Press PS on the controller to pair.

Full step-by-step setup and troubleshooting: [CYD_STEPS.md](CYD_STEPS.md).

## Credits
Based on the [esp-idf-arduino-bluepad32-template](https://github.com/ricardoquesada/esp-idf-arduino-bluepad32-template) by Ricardo Quesada (Apache-2.0, see `LICENSE`).
