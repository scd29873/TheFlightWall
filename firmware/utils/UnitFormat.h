#pragma once

// Arduino-free pure helpers (host-testable). No String, no Arduino.h.
//
// Which unit each measured quantity is SHOWN in, chosen per quantity in the web
// UI. FlightInfo keeps aviation units throughout (ft, kt, ft/min, km -- what the
// fetchers produce); conversion happens only here, at the moment a number
// becomes text, so no fetcher, cache or filter has to know the display choice.
//
// The defaults reproduce what the Mini and Wide cards always showed -- ft, mph,
// ft/s -- so a board that never opens the Units section renders exactly as
// before on those cards. The smaller layouts used to hard-code kt and ft/min
// instead; they now follow the same settings, which is the point: one choice,
// every surface.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

enum class AltitudeUnit : uint8_t
{
    Feet = 0,
    Metres = 1,
};

enum class SpeedUnit : uint8_t
{
    Knots = 0,
    Mph = 1,
    Kmh = 2,
};

enum class ClimbUnit : uint8_t
{
    FeetPerMin = 0,
    FeetPerSec = 1,
    MetresPerSec = 2,
};

enum class DistanceUnit : uint8_t
{
    Km = 0,
    Miles = 1,
    NauticalMiles = 2,
};

// ---- Names, as stored in settings JSON and posted by the web UI -------------
// An unknown name falls back to the default for that quantity, never to an
// error: a settings file from a newer build must still load.

inline const char *unitName(AltitudeUnit u) { return u == AltitudeUnit::Metres ? "m" : "ft"; }
inline const char *unitName(SpeedUnit u)
{
    return u == SpeedUnit::Knots ? "kt" : u == SpeedUnit::Kmh ? "kmh" : "mph";
}
inline const char *unitName(ClimbUnit u)
{
    return u == ClimbUnit::FeetPerMin ? "fpm" : u == ClimbUnit::MetresPerSec ? "mps" : "fps";
}
inline const char *unitName(DistanceUnit u)
{
    return u == DistanceUnit::Miles ? "mi" : u == DistanceUnit::NauticalMiles ? "nm" : "km";
}

inline AltitudeUnit altitudeUnitFromName(const char *s)
{
    return (s && strcmp(s, "m") == 0) ? AltitudeUnit::Metres : AltitudeUnit::Feet;
}
inline SpeedUnit speedUnitFromName(const char *s)
{
    if (s && strcmp(s, "kt") == 0)
        return SpeedUnit::Knots;
    if (s && strcmp(s, "kmh") == 0)
        return SpeedUnit::Kmh;
    return SpeedUnit::Mph;
}
inline ClimbUnit climbUnitFromName(const char *s)
{
    if (s && strcmp(s, "fpm") == 0)
        return ClimbUnit::FeetPerMin;
    if (s && strcmp(s, "mps") == 0)
        return ClimbUnit::MetresPerSec;
    return ClimbUnit::FeetPerSec;
}
inline DistanceUnit distanceUnitFromName(const char *s)
{
    if (s && strcmp(s, "mi") == 0)
        return DistanceUnit::Miles;
    if (s && strcmp(s, "nm") == 0)
        return DistanceUnit::NauticalMiles;
    return DistanceUnit::Km;
}

// ---- Formatters ----------------------------------------------------------------
// Each writes a NUL-terminated string into out[n] and returns out. Callers have
// already checked the value is renderable (not NaN) and handled the vertical-rate
// direction-only sentinel; these only turn a number into text.
//
// `unit == false` drops the suffix: the Mini/Wide metric rows try that before
// they would otherwise truncate, because a bare number reads better than an
// ellipsis in the middle of one.
//
// Rounding and constants for the feet/mph/ft-s paths are the expressions the
// display used before these existed, character for character, so the default
// units render byte-identically.

// Round half away from zero, as the display always has for signed values.
inline long roundAway(double v) { return (long)(v + (v >= 0 ? 0.5 : -0.5)); }

// Metres, to the nearest 10: ADS-B altitude comes in 25 ft steps (about 7.6 m),
// so a 1 m figure would claim precision the source does not have.
inline long altitudeMetres(double ft) { return roundAway(ft * 0.3048 / 10.0) * 10; }

// The metric rows of the Mini and Wide cards: "35.0kft" / "800ft", or "10670m".
inline const char *formatAltitudeCompact(char *out, size_t n, double ft, AltitudeUnit u, bool unit)
{
    if (u == AltitudeUnit::Metres)
        snprintf(out, n, unit ? "%ldm" : "%ld", altitudeMetres(ft));
    else if (ft >= 1000)
        snprintf(out, n, unit ? "%.1fkft" : "%.1fk", ft / 1000.0);
    else
        snprintf(out, n, unit ? "%ldft" : "%ld", (long)(ft + 0.5));
    return out;
}

// One line of the smaller layouts: "FL350" / "12500ft", or "10670m". Flight
// levels are a feet convention (a pressure altitude in hundreds of feet), so
// metres get no equivalent.
inline const char *formatAltitudeLine(char *out, size_t n, double ft, AltitudeUnit u)
{
    if (u == AltitudeUnit::Metres)
    {
        snprintf(out, n, "%ldm", altitudeMetres(ft));
        return out;
    }
    const long whole = (long)(ft + 0.5);
    if (whole >= 18000)
        snprintf(out, n, "FL%ld", (long)((whole + 50) / 100));
    else
        snprintf(out, n, "%ldft", whole);
    return out;
}

// Ground speed from knots: "445kt", "512mph", "824km/h".
inline const char *formatSpeed(char *out, size_t n, double kt, SpeedUnit u, bool unit)
{
    double v = kt;
    const char *suffix = "kt";
    if (u == SpeedUnit::Mph)
    {
        v = kt * 1.15078;
        suffix = "mph";
    }
    else if (u == SpeedUnit::Kmh)
    {
        v = kt * 1.852;
        suffix = "km/h";
    }
    snprintf(out, n, "%ld%s", (long)(v + 0.5), unit ? suffix : "");
    return out;
}

// Vertical rate from ft/min: "640fpm", "11ft/s", "3.3m/s". `plus` prefixes a
// climb with "+" (the one-line layouts do; the metric rows never have).
inline const char *formatClimb(char *out, size_t n, double fpm, ClimbUnit u, bool unit, bool plus)
{
    if (u == ClimbUnit::MetresPerSec)
    {
        // One decimal: a gentle 500 ft/min is 2.5 m/s, and whole metres per
        // second would round most descents on approach to the same few values.
        const double ms = roundAway(fpm * 0.00508 * 10.0) / 10.0;
        snprintf(out, n, "%s%.1f%s", (plus && ms > 0) ? "+" : "", ms, unit ? "m/s" : "");
        return out;
    }
    const long v = (u == ClimbUnit::FeetPerSec) ? roundAway(fpm / 60.0) : roundAway(fpm);
    snprintf(out, n, "%s%ld%s", (plus && v > 0) ? "+" : "", v,
             unit ? (u == ClimbUnit::FeetPerSec ? "ft/s" : "fpm") : "");
    return out;
}

// Distance from km: "4.2km", "2.6mi", "2.3nm".
inline const char *formatDistance(char *out, size_t n, double km, DistanceUnit u)
{
    double v = km;
    const char *suffix = "km";
    if (u == DistanceUnit::Miles)
    {
        v = km / 1.609344;
        suffix = "mi";
    }
    else if (u == DistanceUnit::NauticalMiles)
    {
        v = km / 1.852;
        suffix = "nm";
    }
    snprintf(out, n, "%.1f%s", v, suffix);
    return out;
}
