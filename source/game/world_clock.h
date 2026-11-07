#pragma once

#include <cstdint>

// The time of day the game shows: the PC's local clock, read live (the
// retail running clock is [ENC] and the port had none - the pause menu showed
// the static weather preset's time once per load, and the modes without a
// weather read 00:00). -clock HH.MM freezes it at a fixed time (the same
// "%d.%d" shape as the game_weathers preset names) for tests / screenshots.
//
// Consumers: PauseMenu::refreshClock (the card's clock and day/night icons),
// the weather preset pick of the drive modes (the nearest preset hour at
// load) and the traffic density hour.

int32_t WorldClockHours();   // 0..23
int32_t WorldClockMinutes(); // 0..59
bool WorldClockIsDay();      // 07:00..19:59, the pause clock's icon rule

// The running game clock (weather_cycle.h) publishes its time every tick; the
// readers below then follow it (the -clock HH.MM override still wins).
void SetWorldClock( int32_t hours, int32_t minutes );
