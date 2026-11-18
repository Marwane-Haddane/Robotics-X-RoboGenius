// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>
#include "memory_game.h"

namespace CydDisplay {
// Initializes the CYD screen. Bluetooth still works if screen setup fails.
bool begin();
void setGame(const MemoryGame::Game& game, bool connected, bool inputReady,
             uint32_t buttons, uint32_t now);
// Call on the Arduino task; drawing is never done on the Bluetooth task.
void update();
}  // namespace CydDisplay
