// Compile-time behavior tests against the actual firmware state machine.
#include "memory_game.h"
using MemoryGame::Game;
using MemoryGame::Stage;
using MemoryGame::Failure;

constexpr void start(Game& game, uint32_t& now, bool hard = false) {
    game.update(now++, true, 0);
    if (hard) {
        game.update(now++, true, 1u << 11);
        game.update(now++, true, 0);
    }
    game.update(now++, true, 1);
    game.update(now++, true, 0);
}

constexpr void watch(Game& game, uint32_t& now, uint32_t held = 0) {
    now += 1000; game.update(now, true, held);
    for (unsigned i = 0; i < game.length; ++i) {
        now += game.flashMs(); game.update(now, true, held);
        now += game.gapMs(); game.update(now, true, held);
    }
}

constexpr bool correct_and_hold() {
    Game game(5); uint32_t now = 0; start(game, now); watch(game, now);
    if (game.stage != Stage::Repeat || game.length != 2 || game.progress) return false;
    const auto first = 1u << game.sequence[0];
    game.update(++now, true, first);
    game.update(++now, true, first);
    if (game.progress != 1 || game.stage != Stage::Repeat) return false;
    game.update(++now, true, 0);
    game.update(++now, true, 1u << game.sequence[1]);
    return game.stage == Stage::Won && game.progress == 2;
}
static_assert(correct_and_hold(), "Held buttons must count only once; correct order wins");

constexpr bool playback_holds() {
    Game game; uint32_t now = 0; start(game, now);
    watch(game, now, 1u << game.sequence[0]);
    game.update(++now, true, 1u << game.sequence[0]);
    if (game.progress || !game.waitingForRelease) return false;
    game.update(++now, true, 0);
    game.update(++now, true, 1u << game.sequence[0]);
    return game.progress == 1;
}
static_assert(playback_holds(), "Playback presses must not leak into the player's turn");

constexpr bool repeated_symbols() {
    Game game; uint32_t now = 0; start(game, now);
    game.sequence[0] = game.sequence[1] = 0; watch(game, now);
    game.update(++now, true, 1); game.update(++now, true, 1);
    if (game.progress != 1) return false;
    game.update(++now, true, 0); game.update(++now, true, 1);
    return game.stage == Stage::Won;
}
static_assert(repeated_symbols(), "Identical consecutive symbols need separate presses");

constexpr bool wrong_and_chord() {
    Game wrong; uint32_t now = 0; start(wrong, now); watch(wrong, now);
    wrong.update(++now, true, 1u << ((wrong.sequence[0] + 1) % 4));
    if (wrong.stage != Stage::Lost || wrong.failure != Failure::Wrong) return false;
    Game chord; now = 0; start(chord, now); watch(chord, now);
    chord.update(++now, true, 3);
    return chord.stage == Stage::Lost && chord.failure == Failure::Chord;
}
static_assert(wrong_and_chord(), "Wrong buttons and simultaneous presses fail clearly");

constexpr bool difficulty_and_timeout() {
    Game hard; uint32_t now = 0; start(hard, now, true); watch(hard, now);
    if (hard.length != 4 || hard.gameMask() != 255 || hard.flashMs() >= 750) return false;
    for (unsigned i = 0; i < hard.length; ++i) if (hard.sequence[i] >= 8) return false;
    now += 4999; hard.update(now, true, 0);
    if (hard.stage != Stage::Repeat || hard.secondsLeft(now) != 1) return false;
    hard.update(++now, true, 0);
    if (hard.stage != Stage::Lost || hard.failure != Failure::Timeout) return false;
    Game easy; now = 0; start(easy, now); watch(easy, now);
    easy.update(now + 600000, true, 0);
    return easy.stage == Stage::Repeat;
}
static_assert(difficulty_and_timeout(), "Only Hard has a five-second per-button deadline");

constexpr bool hard_deadline_refresh_and_retry() {
    Game game; uint32_t now = 0; start(game, now, true); watch(game, now);
    now += 4000; game.update(now, true, 1u << game.sequence[0]);
    game.update(++now, true, 0);
    now += 4000; game.update(now, true, 0);
    if (game.stage != Stage::Repeat || game.progress != 1) return false;
    game.update(++now, true, 1u << ((game.sequence[1] + 1) % 8));
    if (game.stage != Stage::Lost) return false;
    game.update(++now, true, 0); game.update(++now, true, 1);
    return game.stage == Stage::Ready && game.hard && game.round == 1 &&
           game.length == 4 && game.progress == 0 && game.failure == Failure::None;
}
static_assert(hard_deadline_refresh_and_retry(), "Correct Hard presses refresh the deadline; retry resets the round");

constexpr bool disconnect_and_menu() {
    Game game; uint32_t now = 0; start(game, now); watch(game, now);
    game.update(++now, false, 0);
    game.update(++now, true, 1);
    if (game.stage != Stage::Menu || game.round) return false;
    game.update(++now, true, 0); game.update(++now, true, 1);
    if (game.stage != Stage::Ready) return false;
    game.update(++now, true, 1u << 15);
    return game.stage == Stage::Menu;
}
static_assert(disconnect_and_menu(), "Reconnect requires release; Options exits even playback");

constexpr bool bounds_and_prefix() {
    Game game(12345); uint32_t now = 0; start(game, now);
    uint8_t prefix[Game::kMaxLength] = {};
    unsigned oldLength = 0;
    for (unsigned round = 1; round <= 11; ++round) {
        if (game.length != round + 1 || game.round != round) return false;
        for (unsigned i = 0; i < oldLength; ++i) if (game.sequence[i] != prefix[i]) return false;
        for (unsigned i = 0; i < game.length; ++i) prefix[i] = game.sequence[i];
        oldLength = game.length;
        watch(game, now);
        for (unsigned i = 0; i < game.length; ++i) {
            game.update(++now, true, 1u << game.sequence[i]);
            game.update(++now, true, 0);
        }
        if (game.length == Game::kMaxLength)
            return game.stage == Stage::Complete;
        if (game.stage != Stage::Won) return false;
        game.update(++now, true, 1); game.update(++now, true, 0);
    }
    return false;
}
static_assert(bounds_and_prefix(), "Rounds extend the same sequence and stop safely at twelve");

constexpr bool clock_wrap_and_late_frame() {
    Game game; uint32_t now = UINT32_MAX - 500; start(game, now, true); watch(game, now);
    now += 5000; game.update(now, true, 0);
    if (game.stage != Stage::Lost) return false;
    Game late; now = 0; start(late, now);
    now += 1000; late.update(now, true, 0);
    now += 10000; late.update(now, true, 0);
    return late.stage == Stage::Watch && late.shown == 0 && !late.lit;
}
static_assert(clock_wrap_and_late_frame(), "Timers survive wraparound and never skip visible symbols");
