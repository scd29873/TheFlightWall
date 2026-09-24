/*
Purpose: See ReadsbAircraft.h. Moved here from AdsbLolFetcher, which used to
hold this logic inline, so that LocalReceiverFetcher parses the same rows the
same way. The one addition is dropping readsb's '~' non-ICAO address marker.
*/
#include "adapters/ReadsbAircraft.h"
#include "utils/GeoUtils.h"
#include "utils/JsonOptional.h"
#include <algorithm>

// The feeds report imperial/nautical; StateVector's contract is SI (OpenSky's
// units), so convert on the way in exactly as FlightRadar24Fetcher does.
static constexpr double kFeetToMeters = 1.0 / 3.28084;
static constexpr double kKnotsToMetersPerSec = 1.0 / 1.94384;
static constexpr double kFpmToMetersPerSec = 1.0 / 196.850;

// Numeric reads go through optNum() in utils/JsonOptional.h, which type-checks
// rather than only null-checking. That matters here specifically: these are
// feeds we do not control, and `.isNull() ? NAN : .as<double>()` silently
// coerces a present-but-wrong-typed value -- a JSON true becomes 1.0, an array
// or object becomes 0.0. Zero in an altitude field renders as sea level, which
// is the same "silently renders as fact" failure the "ground" string sentinel
// below exists to prevent, arriving through a quieter door.
bool readsbAircraftToState(JsonObject a, StateVector &s)
{
    s.lat = optNum(a, "lat");
    s.lon = optNum(a, "lon");
    if (isnan(s.lat) || isnan(s.lon))
        return false;

    s.icao24 = String(a["hex"] | "");
    s.icao24.toLowerCase();
    // readsb marks addresses that are not ICAO24 (TIS-B, anonymised) with a
    // leading '~'. Nothing downstream can look those up, and the character
    // would only end up in a cache key.
    if (s.icao24.startsWith("~"))
        s.icao24.remove(0, 1);
    s.callsign = String(a["flight"] | "");
    s.callsign.trim();

    // alt_baro is the STRING "ground" for surface aircraft, not a number.
    // Reading it as a number yields 0, which renders as sea level. This is
    // a documented SENTINEL, not a type error, so it is handled separately
    // from -- and before -- the type-safe optNum() read below.
    JsonVariant alt = a["alt_baro"];
    if (alt.is<const char *>())
    {
        s.on_ground = true;
        s.baro_altitude = NAN;
    }
    else
    {
        s.on_ground = false;
        double ft = optNum(a, "alt_baro");
        if (isnan(ft))
            ft = optNum(a, "alt_geom");
        s.baro_altitude = isnan(ft) ? NAN : ft * kFeetToMeters;
    }
    s.geo_altitude = s.baro_altitude;

    double gsKt = optNum(a, "gs");
    s.velocity = isnan(gsKt) ? NAN : gsKt * kKnotsToMetersPerSec;
    s.heading = optNum(a, "track");

    // The "ground" sentinel governs the vertical rate too, not just altitude
    // above. A surface aircraft has no climb rate, and the two fallback
    // chains are otherwise identical -- leaving this one ungated let a
    // rebroadcast row report a rate while its altitude had been discarded
    // as unknowable. Matches server/src/adsblol.ts, which parses the same
    // feed independently.
    double rateFpm = NAN;
    if (!s.on_ground)
    {
        rateFpm = optNum(a, "baro_rate");
        if (isnan(rateFpm))
            rateFpm = optNum(a, "geom_rate");
    }
    s.vertical_rate = isnan(rateFpm) ? NAN : rateFpm * kFpmToMetersPerSec;

    // Inline when the feed has an aircraft database (adsb.lol always; readsb
    // with --db-file), and then it replaces the per-flight aircraft lookup:
    // type is keyed by ICAO24 (the airframe), the one enrichment field that
    // was already 100% reliable. dump1090-fa has no database, so this stays
    // empty there and the lookup runs as it does for OpenSky.
    s.aircraft_type = String(a["t"] | "");

    // These feeds encode the ADS-B emitter category as a STRING ("A7" =
    // rotorcraft); OpenSky uses an integer (8 = rotorcraft) and
    // StateVector::category is the OpenSky integer. Translate, or the
    // helicopter check is silently dead for these sources.
    //
    // Only category-SET A is mapped here. Live sampling also shows
    // category-set B and beyond on the wire (e.g. "B4", ultralight/
    // hang-glider under the ADS-B emitter-category scheme) -- those are
    // real, valid categories, just not ones OpenSky's integer scheme (or
    // the helicopter check) has a slot for. The cat[0]=='A' guard below
    // already leaves s.category at its 0 default for anything outside set
    // A, which is the correct outcome: deliberately ignored, not mismapped
    // onto a set-A meaning that doesn't apply to it.
    const char *cat = a["category"] | "";
    if (cat[0] == 'A' && cat[1] >= '0' && cat[1] <= '7')
        s.category = (cat[1] - '0') + 1; // A0->1 .. A7->8, matching OpenSky

    // NOT set: neither feed carries a route, so enrichment must still run.
    // hasInlineRoute() means "the feed carried a ROUTE".
    return true;
}

size_t readsbNearestInRadius(JsonArray rows, double centerLat, double centerLon, double radiusKm,
                             double maxPositionAgeSec, size_t maxCount,
                             std::vector<StateVector> &out)
{
    size_t heard = 0;
    std::vector<StateVector> inRadius;
    for (JsonObject a : rows)
    {
        heard++;
        const double age = optNum(a, "seen_pos");
        if (!isnan(age) && age > maxPositionAgeSec)
            continue;

        StateVector s;
        if (!readsbAircraftToState(a, s))
            continue; // heard, but no position to place it by

        s.distance_km = haversineKm(centerLat, centerLon, s.lat, s.lon);
        if (s.distance_km > radiusKm)
            continue;
        s.bearing_deg = computeBearingDeg(centerLat, centerLon, s.lat, s.lon);
        inRadius.push_back(s);
    }

    // Nearest first only matters when the cap bites; the display pipeline sorts
    // its candidates anyway.
    if (inRadius.size() > maxCount)
    {
        std::partial_sort(inRadius.begin(), inRadius.begin() + maxCount, inRadius.end(),
                          [](const StateVector &x, const StateVector &y)
                          { return x.distance_km < y.distance_km; });
        inRadius.resize(maxCount);
    }
    for (StateVector &s : inRadius)
        out.push_back(std::move(s));
    return heard;
}
