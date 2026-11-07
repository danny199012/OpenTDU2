#pragma once

#include <cstddef>

// Unit system of the driving readouts (metric / imperial).
//
// The retail game ships both: GAUGES carries the `/:SpeedUnit` text plus two
// needle tables (`interior_kmh` and `interior_mph`, docs/re/freeride_spec.md 4b)
// and DASHBRD has a `/:Distance` field, so the unit is a player setting there.
// The port keeps it in GameOptions::Units (saved in options.ini), turns it with
// `-imperial` / `-metric` on the command line and with the cheats panel row, and
// every readout that shows a speed or a distance goes through these helpers so
// the numbers cannot disagree: the speed digits and the odometer of the GAUGES
// movie, the port's fallback gauge, the fuel range, the GPS / detector lines.
//
// [INF: the retail pick is the language profile of the game (the same speed
// becomes miles in the US / UK build), which the port does not have; the
// options.ini flag is the port's own.]

bool IsImperialUnits();
// Also writes options.ini (the cheats panel row).
void SetImperialUnits( bool bImperial );

// km/h -> the displayed speed (mph in imperial).
float SpeedForDisplay( float speedKmh );
// km -> the displayed distance in the same unit system (mi in imperial).
float KmForDisplay( float km );
// metres -> km or miles.
float MetresForDisplay( float metres );

// "km/h" / "mph".
const char *SpeedUnitLabel();
// "km" / "mi".
const char *DistanceUnitLabel();

// A distance in metres written for the HUD: "850 m" / "12.3 km" / "2,800 ft" /
// "7.6 mi" (the threshold is the unit's own thousand: 1000 m, 1609 m).
void FormatDistance( char *pBuffer, size_t size, float metres );
