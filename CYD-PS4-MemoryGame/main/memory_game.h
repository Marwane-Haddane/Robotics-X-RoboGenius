// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdint.h>

namespace MemoryGame {
enum class Stage : uint8_t { Menu, Ready, Watch, Repeat, Won, Lost, Complete };
enum class Failure : uint8_t { None, Wrong, Chord, Timeout };

// No hardware or drawing here: the same state machine is exercised by the tests.
class Game {
public:
    static constexpr uint8_t kMaxLength = 12;
    Stage stage = Stage::Menu;
    Failure failure = Failure::None;
    bool hard = false, waitingForRelease = true;
    uint8_t sequence[kMaxLength] = {};
    uint8_t length = 0, round = 0, progress = 0, shown = 0;
    bool lit = false;

    constexpr explicit Game(uint32_t seed = 1) : randomState(seed ? seed : 1) {}
    constexpr uint32_t gameMask() const { return hard ? 0xff : 0x0f; }
    constexpr uint32_t flashMs() const { return hard ? 350 : 750; }
    constexpr uint32_t gapMs() const { return hard ? 180 : 250; }
    constexpr uint32_t lights(uint32_t buttons) const {
        return stage == Stage::Watch ? (lit ? (1u << sequence[shown]) : 0) :
               stage == Stage::Repeat ? buttons & gameMask() : 0;
    }
    constexpr uint16_t secondsLeft(uint32_t now) const {
        if (!hard || stage != Stage::Repeat) return 0;
        const uint32_t elapsed = now - inputSince;
        return elapsed >= 5000 ? 0 : (5000 - elapsed + 999) / 1000;
    }

    constexpr void update(uint32_t now, bool ready, uint32_t buttons) {
        const uint32_t rising = buttons & ~previous;
        previous = buttons;
        if (!ready) {
            stage = Stage::Menu; failure = Failure::None;
            length = round = progress = 0;
            controlsReleased = false; waitingForRelease = true;
            return;
        }
        // Do not start a game with a button held while connecting/reconnecting.
        if (!controlsReleased) {
            if (!buttons) controlsReleased = true;
            return;
        }
        if (rising & (1u << 15)) { stage = Stage::Menu; failure = Failure::None; return; }
        if (stage == Stage::Menu) {
            if (rising & ((1u << 8) | (1u << 10))) hard = false;
            else if (rising & ((1u << 9) | (1u << 11))) hard = true;
            if (rising & 1u) start(now);
            return;
        }
        if (stage == Stage::Won || stage == Stage::Lost || stage == Stage::Complete) {
            if (rising & 1u) {
                if (stage == Stage::Won) {
                    ++round; append(); beginPlayback(now);
                } else start(now);
            }
            return;
        }
        if (stage == Stage::Ready) {
            if (now - phaseSince >= 1000) {
                stage = Stage::Watch; shown = 0; lit = true; phaseSince = now;
            }
            return;
        }
        if (stage == Stage::Watch) {
            if (now - phaseSince < (lit ? flashMs() : gapMs())) return;
            phaseSince = now;
            if (lit) lit = false;
            else if (++shown < length) lit = true;
            else {
                stage = Stage::Repeat; progress = 0; inputSince = now;
                waitingForRelease = (buttons & gameMask()) != 0;
            }
            return;
        }
        if (hard && now - inputSince >= 5000) { lose(Failure::Timeout); return; }
        const uint32_t held = buttons & gameMask();
        if (waitingForRelease) {
            if (!held) waitingForRelease = false;
            return;
        }
        if (!(rising & gameMask())) return;
        if (held & (held - 1)) { lose(Failure::Chord); return; }
        if (held != (1u << sequence[progress])) { lose(Failure::Wrong); return; }
        ++progress; waitingForRelease = true; inputSince = now;
        if (progress == length) stage = length == kMaxLength ? Stage::Complete : Stage::Won;
    }

private:
    uint32_t randomState, previous = 0, phaseSince = 0, inputSince = 0;
    bool controlsReleased = false;
    constexpr void append() {
        randomState ^= randomState << 13;
        randomState ^= randomState >> 17;
        randomState ^= randomState << 5;
        sequence[length++] = randomState % (hard ? 8 : 4);
    }
    constexpr void start(uint32_t now) {
        length = 0; round = 1;
        const unsigned initialLength = hard ? 4 : 2;
        for (unsigned i = 0; i < initialLength; ++i) append();
        beginPlayback(now);
    }
    constexpr void beginPlayback(uint32_t now) {
        stage = Stage::Ready; failure = Failure::None;
        shown = progress = 0; lit = false; phaseSince = now;
        waitingForRelease = true;
    }
    constexpr void lose(Failure why) { failure = why; stage = Stage::Lost; }
};
}  // namespace MemoryGame
