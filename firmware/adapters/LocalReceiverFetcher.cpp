/*
Purpose: Fetch live positions from your own receiver's aircraft.json.
See LocalReceiverFetcher.h for why this source exists and what it carries.

File shape (dump1090-fa, readsb and dump1090 agree on it):
  { "now": <unix s>, "messages": N,
    "aircraft": [ { hex, flight, lat, lon, alt_baro, alt_geom, gs, track,
                    baro_rate, geom_rate, category, seen, seen_pos, [t, r] }, ... ] }
Rows are parsed by readsbAircraftToState(), shared with adsb.lol.
*/
#include "adapters/LocalReceiverFetcher.h"
#include "adapters/ReadsbAircraft.h"
#include "core/Settings.h"
#include <ArduinoJson.h>
#include <esp_heap_caps.h>

// Where the common receiver packages serve aircraft.json, most likely first.
static const char *const kJsonPaths[] = {
    "/data/aircraft.json",             // PiAware's :8080 map; readsb's own web server
    "/skyaware/data/aircraft.json",    // PiAware's port-80 SkyAware page
    "/tar1090/data/aircraft.json",     // readsb + tar1090
    "/dump1090-fa/data/aircraft.json", // older dump1090-fa packages
    "/dump1090/data/aircraft.json",    // dump1090-mutability
};

// The file keeps an aircraft for minutes after its last message, so a row's
// position can be well out of date; one older than this is where the aircraft
// WAS, and its "distance" would be a guess.
static constexpr double kMaxPositionAgeSec = 60.0;

// Same safety cap as the other state sources. Applied to the NEAREST rows: the
// file lists everything the antenna hears, often hundreds of kilometres out.
static constexpr size_t kMaxFlights = 40;

// On a LAN a receiver that has not answered in this long is down, not slow;
// failing fast keeps a powered-off Pi from stalling the display loop.
static constexpr int32_t kConnectTimeoutMs = 3000;
static constexpr uint16_t kReadTimeoutMs = 5000;

#if defined(BOARD_HAS_PSRAM)
namespace
{
// A busy receiver's rows add up; keep the parsed document out of the internal
// RAM that TLS and the panel's DMA compete for (same as AdsbLolFetcher).
struct PsramAllocator : ArduinoJson::Allocator
{
    void *allocate(size_t n) override { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM); }
    void deallocate(void *p) override { heap_caps_free(p); }
    void *reallocate(void *p, size_t n) override { return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM); }
};
} // namespace
#endif

bool LocalReceiverFetcher::fetchStateVectors(double centerLat,
                                             double centerLon,
                                             double radiusKm,
                                             std::vector<StateVector> &outStateVectors)
{
    const String &base = g_settings.receiverUrl;
    if (base.length() == 0)
    {
        Serial.println("LocalReceiverFetcher: no receiver address set "
                       "(web UI: Setup -> Position source -> Your own receiver)");
        return false;
    }

    int code = 0;

    // A full URL is used exactly as given.
    if (base.endsWith(".json"))
        return fetchFrom(base, centerLat, centerLon, radiusKm, outStateVectors, code);

    // Only an address: reuse the path that answered last time.
    if (m_resolvedFor == base && m_resolvedUrl.length())
    {
        if (fetchFrom(m_resolvedUrl, centerLat, centerLon, radiusKm, outStateVectors, code))
            return true;
        // Down or slow is not "moved": searching every path would just repeat
        // the failure five times. Only a 404 means the file went elsewhere
        // (a different receiver package), which is worth a fresh search.
        if (code != 404)
            return false;
        m_resolvedUrl = "";
    }

    for (const char *path : kJsonPaths)
    {
        const String url = base + path;
        if (fetchFrom(url, centerLat, centerLon, radiusKm, outStateVectors, code))
        {
            m_resolvedFor = base;
            m_resolvedUrl = url;
            Serial.printf("LocalReceiverFetcher: using %s\n", url.c_str());
            return true;
        }
        if (code <= 0)
            return false; // nothing answered at all; every path would fail the same way
    }
    Serial.printf("LocalReceiverFetcher: no aircraft.json found under %s -- "
                  "enter the full URL (e.g. %s/data/aircraft.json)\n",
                  base.c_str(), base.c_str());
    return false;
}

bool LocalReceiverFetcher::fetchFrom(const String &url, double centerLat, double centerLon,
                                     double radiusKm, std::vector<StateVector> &outStateVectors,
                                     int &httpCode)
{
    const bool tls = url.startsWith("https://");
    httpCode = 0;

    HTTPClient http;
    bool begun;
    if (tls)
    {
        // Only for a receiver behind an HTTPS reverse proxy. CA not pinned,
        // matching every other fetcher here; the handshake is bounded so a
        // stall fails fast instead of parking loopTask until the watchdog.
        if (!m_secureInit)
        {
            m_secure.setInsecure();
            m_secure.setHandshakeTimeout(15);
            m_secureInit = true;
        }
        m_secure.stop();
        begun = http.begin(m_secure, url);
    }
    else
    {
        m_plain.stop();
        begun = http.begin(m_plain, url);
        // Parse straight off the socket, which needs a body that is not
        // chunked -- HTTP/1.0 guarantees that. Plain HTTP only: over TLS,
        // 1.0's close-delimited body can be truncated (see the note in
        // FlightRadar24Fetcher.cpp), so the https path reads it whole below.
        http.useHTTP10(true);
    }
    if (!begun)
    {
        Serial.printf("LocalReceiverFetcher: bad URL %s\n", url.c_str());
        httpCode = -1;
        return false;
    }
    http.setConnectTimeout(kConnectTimeoutMs);
    http.setTimeout(kReadTimeoutMs);
    http.setUserAgent("TheFlightWall/1.0");

    const unsigned long t0 = millis();
    httpCode = http.GET();
    if (httpCode != 200)
    {
        Serial.printf("LocalReceiverFetcher: %s -> %s after %lums\n", url.c_str(),
                      httpCode > 0 ? String(httpCode).c_str() : HTTPClient::errorToString(httpCode).c_str(),
                      (unsigned long)(millis() - t0));
        http.end();
        return false;
    }

    // Only the fields readsbAircraftToState() reads, plus seen_pos. A receiver
    // with a good antenna lists a few hundred aircraft; the filter keeps the
    // parsed document to a fraction of the file.
    JsonDocument filter;
    for (const char *k : {"hex", "flight", "lat", "lon", "alt_baro", "alt_geom", "gs", "track",
                          "baro_rate", "geom_rate", "category", "t", "seen_pos"})
        filter["aircraft"][0][k] = true;

#if defined(BOARD_HAS_PSRAM)
    static PsramAllocator psramAllocator;
    JsonDocument doc(&psramAllocator);
#else
    JsonDocument doc;
#endif
    const DeserializationError err =
        tls ? deserializeJson(doc, http.getString(), DeserializationOption::Filter(filter))
            : deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();
    if (err)
    {
        // A 200 that is not JSON is usually a web page at the wrong path, which
        // is why httpCode stays 200: the path search moves on to the next one.
        Serial.printf("LocalReceiverFetcher: %s is not aircraft JSON (%s)\n", url.c_str(), err.c_str());
        return false;
    }

    JsonArray rows = doc["aircraft"].as<JsonArray>();
    if (rows.isNull())
    {
        Serial.printf("LocalReceiverFetcher: %s has no 'aircraft' array\n", url.c_str());
        return false;
    }

    const size_t heard = readsbNearestInRadius(rows, centerLat, centerLon, radiusKm,
                                               kMaxPositionAgeSec, kMaxFlights, outStateVectors);
    Serial.printf("[fetch] receiver: %u aircraft heard, %u in radius\n",
                  (unsigned)heard, (unsigned)outStateVectors.size());
    return true;
}
