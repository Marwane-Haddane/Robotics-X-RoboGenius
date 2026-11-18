// SPDX-License-Identifier: Apache-2.0
// CYD memory combo: Bluetooth pairing, difficulty menu, playback and input.
#include <Arduino.h>
#include <Bluepad32.h>
#include "cyd_display.h"
#include "esp_random.h"

#if !defined(CONFIG_IDF_TARGET_ESP32)
#error "DualShock 4 needs Bluetooth Classic. Build the esp32dev environment."
#endif

namespace {
struct PadState {
    ControllerPtr controller = nullptr;
    uint32_t previous = 0;
    bool leftTrigger = false;
    bool rightTrigger = false;
    bool inputReady = false;
};
PadState pads[BP32_MAX_GAMEPADS];
MemoryGame::Game game;
ControllerPtr player = nullptr;
constexpr const char* kNames[] = {
    "X", "O", "SQUARE", "TRIANGLE", "L1", "R1", "L2", "R2",
    "UP", "DOWN", "LEFT", "RIGHT", "L3", "R3", "SHARE", "OPTIONS", "PS"
};

// Trigger values are 0..1023. Separate thresholds avoid jitter near the boundary.
bool triggerPressed(int value, bool wasPressed, bool digital) {
    return digital || value >= (wasPressed ? 100 : 200);
}

void onConnected(ControllerPtr controller) {
    // Bluepad32 4.2 can notify connection before the first input report, so
    // isGamepad() is not populated yet. Model properties are already available.
    if (controller->getModel() != Controller::CONTROLLER_TYPE_PS4Controller) {
        Console.println("Device ignored: this game accepts DualShock 4 controllers.");
        return;
    }
    for (auto& pad : pads) {
        if (pad.controller)
            continue;
        pad = {};
        pad.controller = controller;
        const auto properties = controller->getProperties();
        const String model = controller->getModelName();
        Console.printf("GAMEPAD CONNECTED: %s | VID=%04x PID=%04x | slot=%d\n",
                       model.c_str(), properties.vendor_id, properties.product_id, controller->index());
        Console.println("PS4 CONTROLLER CONFIRMED. Test X, O, SQUARE, TRIANGLE, L1, R1, L2, R2.");
        controller->setColorLED(0, 80, 255);
        return;
    }
    Console.println("No free gamepad slot.");
}

void onDisconnected(ControllerPtr controller) {
    for (auto& pad : pads) {
        if (pad.controller == controller) {
            pad = {};
            if (player == controller) {
                player = nullptr;
                game.update(millis(), false, 0);
            }
            BP32.enableNewBluetoothConnections(true);
            Console.println("GAMEPAD DISCONNECTED. Try PS; if it fails, unplug USB and hold SHARE + PS.");
            return;
        }
    }
}

void reportChanges(PadState& pad) {
    auto controller = pad.controller;
    pad.leftTrigger = triggerPressed(controller->brake(), pad.leftTrigger, controller->l2());
    pad.rightTrigger = triggerPressed(controller->throttle(), pad.rightTrigger, controller->r2());
    const uint8_t dpad = controller->dpad();
    const bool pressed[] = {
        controller->a(), controller->b(), controller->x(), controller->y(),
        controller->l1(), controller->r1(), pad.leftTrigger, pad.rightTrigger,
        bool(dpad & DPAD_UP), bool(dpad & DPAD_DOWN), bool(dpad & DPAD_LEFT), bool(dpad & DPAD_RIGHT),
        controller->thumbL(), controller->thumbR(), controller->miscSelect(),
        controller->miscStart(), controller->miscSystem()
    };
    static_assert(sizeof(pressed) / sizeof(pressed[0]) == sizeof(kNames) / sizeof(kNames[0]));
    uint32_t current = 0;
    for (unsigned i = 0; i < sizeof(pressed) / sizeof(pressed[0]); ++i) {
        if (pressed[i])
            current |= (1UL << i);
    }
    const uint32_t changed = current ^ pad.previous;
    for (unsigned i = 0; i < sizeof(pressed) / sizeof(pressed[0]); ++i) {
        if (changed & (1UL << i)) {
            Console.printf("[%d] %s %s\n", controller->index(), kNames[i], pressed[i] ? "PRESSED" : "RELEASED");
        }
    }
    pad.previous = current;
}
}  // namespace

void setup() {
    // Keep the template's interactive console. Arduino Serial conflicts with it.
    Console.printf("\nCYD MEMORY COMBO | Bluepad32 %s\n", BP32.firmwareVersion());
    const String chip = ESP.getChipModel();
    Console.printf("Chip: %s | Flash: %lu bytes | Free heap: %lu bytes\n",
                   chip.c_str(), static_cast<unsigned long>(ESP.getFlashChipSize()),
                   static_cast<unsigned long>(ESP.getFreeHeap()));
    const uint8_t* address = BP32.localBdAddress();
    Console.printf("Bluetooth address: %02X:%02X:%02X:%02X:%02X:%02X\n",
                   address[0], address[1], address[2], address[3], address[4], address[5]);
    CydDisplay::begin();
    game = MemoryGame::Game(esp_random());
    BP32.setup(&onConnected, &onDisconnected);
    BP32.enableVirtualDevice(false);
    BP32.enableBLEService(false);
    BP32.enableNewBluetoothConnections(true);
    // Retain bonds for reconnection after reboot; no forgetBluetoothKeys() here.
    Console.println("PAIR: unplug controller USB, hold SHARE + PS until the light bar flashes.");
    Console.println("Waiting for controller... Console command: help");
    Console.println("MENU: D-pad chooses EASY/HARD; X starts; OPTIONS returns to menu.");
}

void loop() {
    if (BP32.update()) {
        for (auto& pad : pads) {
            // Only read gamepad fields after a real gamepad input report arrives.
            if (pad.controller && pad.controller->isConnected() && pad.controller->hasData() &&
                pad.controller->isGamepad()) {
                if (!pad.inputReady) {
                    pad.inputReady = true;
                    Console.println("PS4 INPUT READY. Press and release buttons to check input.");
                }
                reportChanges(pad);
            }
        }
    }
    bool connected = false, inputReady = false;
    uint32_t buttons = 0;
    for (const auto& pad : pads) {
        if (!pad.controller || !pad.controller->isConnected()) continue;
        // One player owns this round. Other connected pads cannot alter the combo.
        if (!player) player = pad.controller;
        if (pad.controller != player) continue;
        connected = true;
        inputReady = pad.inputReady;
        buttons = pad.previous;
    }
    const auto previousStage = game.stage;
    const uint32_t now = millis();
    game.update(now, connected && inputReady, buttons);
    if (game.stage != previousStage) {
        constexpr const char* stages[] = {"MENU", "GET READY", "WATCH", "YOUR TURN", "ROUND CLEAR", "TRY AGAIN", "COMPLETE"};
        Console.printf("GAME: %s | %s | round=%u | length=%u | reason=%u\n",
                       stages[static_cast<unsigned>(game.stage)], game.hard ? "HARD" : "EASY",
                       game.round, game.length, static_cast<unsigned>(game.failure));
    }
    CydDisplay::setGame(game, connected, inputReady, buttons, now);
    CydDisplay::update();
    // Frequent polling catches short presses; yielding keeps the watchdog satisfied.
    delay(1);
}
