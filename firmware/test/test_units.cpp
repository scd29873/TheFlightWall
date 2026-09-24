// Host unit tests for UnitFormat.h — compile with g++, no hardware.
//
// Guarded like the other loose host tests under test/; see platformio.ini.
//
// The feet/mph/ft-s cases are pinned to exactly what the display printed before
// units were selectable (miniAlt, miniSpdMph, miniVr, formatAltitude and
// formatVerticalRate in Hub75Display.cpp), because the defaults promise that a
// board which never touches the Units section renders byte-identically.
#ifndef PIO_UNIT_TESTING
#include "../utils/UnitFormat.h"
#include <cstdio>
#include <cstring>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)
#define CHECK_STR(expr, want) do { const char *got_ = (expr); \
    if (strcmp(got_, want) != 0) { printf("FAIL %s:%d  %s -> \"%s\", want \"%s\"\n", __FILE__, __LINE__, #expr, got_, want); failures++; } } while (0)

int main() {
    char b[32];
    const size_t n = sizeof(b);

    // ---- Altitude, compact (Mini/Wide metric rows) ----
    CHECK_STR(formatAltitudeCompact(b, n, 35000, AltitudeUnit::Feet, true), "35.0kft");
    CHECK_STR(formatAltitudeCompact(b, n, 35000, AltitudeUnit::Feet, false), "35.0k");
    CHECK_STR(formatAltitudeCompact(b, n, 999.4, AltitudeUnit::Feet, true), "999ft");
    CHECK_STR(formatAltitudeCompact(b, n, 800, AltitudeUnit::Feet, false), "800");
    CHECK_STR(formatAltitudeCompact(b, n, 12000, AltitudeUnit::Metres, true), "3660m");   // 3657.6 -> nearest 10
    CHECK_STR(formatAltitudeCompact(b, n, 35000, AltitudeUnit::Metres, true), "10670m");  // 10668
    CHECK_STR(formatAltitudeCompact(b, n, 35000, AltitudeUnit::Metres, false), "10670");
    CHECK_STR(formatAltitudeCompact(b, n, -50, AltitudeUnit::Metres, true), "-20m");      // below sea level (AMS)

    // ---- Altitude, one-line layouts ----
    CHECK_STR(formatAltitudeLine(b, n, 35000, AltitudeUnit::Feet), "FL350");
    CHECK_STR(formatAltitudeLine(b, n, 17999.6, AltitudeUnit::Feet), "FL180");            // rounds to 18000 first
    CHECK_STR(formatAltitudeLine(b, n, 12500, AltitudeUnit::Feet), "12500ft");
    CHECK_STR(formatAltitudeLine(b, n, 35000, AltitudeUnit::Metres), "10670m");          // no flight levels in metres

    // ---- Speed from knots ----
    CHECK_STR(formatSpeed(b, n, 445, SpeedUnit::Mph, true), "512mph");                    // the old miniSpdMph
    CHECK_STR(formatSpeed(b, n, 445, SpeedUnit::Mph, false), "512");
    CHECK_STR(formatSpeed(b, n, 445, SpeedUnit::Knots, true), "445kt");
    CHECK_STR(formatSpeed(b, n, 445, SpeedUnit::Kmh, true), "824km/h");
    CHECK_STR(formatSpeed(b, n, 0, SpeedUnit::Kmh, true), "0km/h");

    // ---- Climb rate from ft/min ----
    CHECK_STR(formatClimb(b, n, 640, ClimbUnit::FeetPerSec, true, false), "11ft/s");       // the old miniVr
    CHECK_STR(formatClimb(b, n, -1800, ClimbUnit::FeetPerSec, true, false), "-30ft/s");
    CHECK_STR(formatClimb(b, n, 640, ClimbUnit::FeetPerSec, false, false), "11");
    CHECK_STR(formatClimb(b, n, 1200, ClimbUnit::FeetPerMin, true, true), "+1200fpm");    // the old formatVerticalRate
    CHECK_STR(formatClimb(b, n, -64, ClimbUnit::FeetPerMin, true, true), "-64fpm");
    CHECK_STR(formatClimb(b, n, 0, ClimbUnit::FeetPerMin, true, true), "0fpm");           // level: no "+"
    CHECK_STR(formatClimb(b, n, 640, ClimbUnit::MetresPerSec, true, false), "3.3m/s");     // 3.2512
    CHECK_STR(formatClimb(b, n, 640, ClimbUnit::MetresPerSec, true, true), "+3.3m/s");
    CHECK_STR(formatClimb(b, n, -1800, ClimbUnit::MetresPerSec, true, true), "-9.1m/s");   // -9.144
    CHECK_STR(formatClimb(b, n, -5, ClimbUnit::MetresPerSec, true, true), "0.0m/s");       // never "-0.0"

    // ---- Distance from km ----
    CHECK_STR(formatDistance(b, n, 4.2, DistanceUnit::Km), "4.2km");
    CHECK_STR(formatDistance(b, n, 4.2, DistanceUnit::Miles), "2.6mi");
    CHECK_STR(formatDistance(b, n, 4.2, DistanceUnit::NauticalMiles), "2.3nm");

    // ---- Names round-trip; unknown names fall back to the defaults ----
    CHECK(altitudeUnitFromName(unitName(AltitudeUnit::Metres)) == AltitudeUnit::Metres);
    CHECK(speedUnitFromName(unitName(SpeedUnit::Kmh)) == SpeedUnit::Kmh);
    CHECK(speedUnitFromName(unitName(SpeedUnit::Knots)) == SpeedUnit::Knots);
    CHECK(climbUnitFromName(unitName(ClimbUnit::MetresPerSec)) == ClimbUnit::MetresPerSec);
    CHECK(climbUnitFromName(unitName(ClimbUnit::FeetPerMin)) == ClimbUnit::FeetPerMin);
    CHECK(distanceUnitFromName(unitName(DistanceUnit::NauticalMiles)) == DistanceUnit::NauticalMiles);
    CHECK(altitudeUnitFromName("furlongs") == AltitudeUnit::Feet);
    CHECK(speedUnitFromName(nullptr) == SpeedUnit::Mph);
    CHECK(climbUnitFromName("") == ClimbUnit::FeetPerSec);
    CHECK(distanceUnitFromName("parsec") == DistanceUnit::Km);

    // Truncation is safe: a short buffer still gets a terminated prefix.
    char tiny[4];
    formatSpeed(tiny, sizeof(tiny), 445, SpeedUnit::Kmh, true);
    CHECK(strlen(tiny) == 3);

    if (failures == 0) { printf("test_units: ALL PASS\n"); return 0; }
    printf("test_units: %d FAILURES\n", failures);
    return 1;
}
#endif
