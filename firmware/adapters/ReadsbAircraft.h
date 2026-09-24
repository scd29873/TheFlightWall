#pragma once
/*
Purpose: Turn one aircraft row in the readsb / dump1090 JSON shape into a
StateVector.

Two sources speak this shape: adsb.lol's /v2 API (rows under "ac") and the
aircraft.json a receiver of your own serves -- dump1090-fa under PiAware,
readsb, dump1090 (rows under "aircraft"). The per-row fields and units are the
same: hex, flight, lat/lon, alt_baro ("ground" or feet), alt_geom, gs (kt),
track, baro_rate/geom_rate (ft/min), category ("A0".."A7"), t (ICAO type, when
the feed has a database). One parser, so the "ground" sentinel and the category
translation cannot come to mean different things for the two sources.

readsbAircraftToState() leaves distance and bearing alone: they depend on the
query point, and the two sources differ there -- adsb.lol precomputes them, a
receiver does not, so readsbNearestInRadius() works them out from the centre.
*/
#include <vector>
#include <ArduinoJson.h>
#include "models/StateVector.h"

// Fills `s` from `a`. Returns false, leaving `s` unusable, when the row has no
// position -- which the display cannot use, so both callers skip it.
bool readsbAircraftToState(JsonObject a, StateVector &s);

// For a receiver's whole file: parse every row, drop those whose position is
// older than maxPositionAgeSec (the file keeps aircraft for minutes after
// their last message) or farther than radiusKm from the centre, fill distance
// and bearing, and append the nearest maxCount to `out`. Returns how many rows
// the file held, for the log line.
size_t readsbNearestInRadius(JsonArray rows, double centerLat, double centerLon, double radiusKm,
                             double maxPositionAgeSec, size_t maxCount,
                             std::vector<StateVector> &out);
