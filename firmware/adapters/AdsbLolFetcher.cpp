/*
Purpose: Fetch live flights from adsb.lol's open /v2 API.

Row shape (every field optional in practice):
  hex, flight (callsign), r (registration), t (ICAO type), lat, lon,
  alt_baro, alt_geom, gs, track, baro_rate, geom_rate,
  category ("A0".."A7"), dst (nm from query point), dir (bearing)
*/
#include "adapters/AdsbLolFetcher.h"
#include "core/Settings.h"
#include "utils/GeoUtils.h"
#include "utils/JsonOptional.h"
#include "adapters/ReadsbAircraft.h"
#include <esp_heap_caps.h>

static constexpr const char *kHost = "https://api.adsb.lol";

// adsb.lol reports its distance in nautical miles; StateVector's is km. The
// per-row unit conversions live with the row parser (adapters/ReadsbAircraft).
static constexpr double kNmToKm = 1.852;

// Same safety cap as the other parsers: bounds the output vector, not the parse.
static constexpr size_t kMaxFlights = 40;

#if defined(BOARD_HAS_PSRAM)
namespace
{
struct PsramAllocator : ArduinoJson::Allocator
{
    void *allocate(size_t n) override { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM); }
    void deallocate(void *p) override { heap_caps_free(p); }
    void *reallocate(void *p, size_t n) override { return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM); }
};
} // namespace
#endif

WiFiClientSecure &AdsbLolFetcher::secureClient()
{
    if (!m_secureInit)
    {
        m_secure.setInsecure();           // CA not pinned; matches OpenSky/FR24/HttpJson
        m_secure.setHandshakeTimeout(15); // seconds — bound it against the loop watchdog
        m_secureInit = true;
    }
    m_secure.stop(); // one client, one host at a time
    return m_secure;
}

bool AdsbLolFetcher::fetchStateVectors(double centerLat,
                                       double centerLon,
                                       double radiusKm,
                                       std::vector<StateVector> &outStateVectors)
{
    // adsb.lol takes a radius in NAUTICAL MILES, capped at 250.
    long radiusNm = lround(radiusKm / kNmToKm);
    if (radiusNm < 1) radiusNm = 1;
    if (radiusNm > 250) radiusNm = 250;

    String url = String(kHost) + "/v2/lat/" + String(centerLat, 4) +
                 "/lon/" + String(centerLon, 4) + "/dist/" + String(radiusNm);

    HTTPClient http;
    const unsigned long t0 = millis();
    http.begin(secureClient(), url);
    // HTTP/1.1 deliberately, NOT useHTTP10(true) — see the long note in
    // FlightRadar24Fetcher.cpp. Under 1.0 the body is delimited by connection
    // close, and WiFiClientSecure discards buffered plaintext once close_notify
    // is processed, truncating any response spanning TLS records.
    http.setTimeout(15000);
    http.addHeader("Accept", "application/json");
    // setUserAgent(), NOT addHeader(). THIS IS THE BUG THAT KILLED THE FALLBACK.
    //
    // HTTPClient::addHeader() silently DISCARDS User-Agent -- its first
    // statement is a guard listing the headers "handled by code" (Connection,
    // User-Agent, Host, Authorization), and a match is dropped with no error
    // and no return value to check. So every request this fetcher ever made
    // went out as HTTPClient's default `ESP32HTTPClient`, and adsb.lol answered
    // every one of them `403 User-Agent too generic; include valid contact
    // info.` The fallback has never worked on any board.
    //
    // The evidence was already in the tree: HANDOFF's remote-diagnosis recipe
    // greps the server log for `req_user_agent=ESP32HTTPClient`, which is the
    // device telling us its User-Agent was never ours. Confirmed the other way
    // too -- curl from a laptop sending this exact string gets a 200, so the
    // string was never the problem, the API to set it was.
    http.setUserAgent("TheFlightWall/1.0 (+https://github.com/chaim354/TheFlightWall_OSS)");

    int code = http.GET();
    if (code != 200)
    {
        // A non-200 here reached the server and was answered, so unlike a
        // transport failure the BODY carries the reason -- rate limit, blocked
        // user-agent, bad path. Logging the code alone turned a 403 into a
        // mystery when the server was explaining itself all along.
        String why = http.getString();
        why.replace('\n', ' ');
        if (why.length() > 180)
            why = why.substring(0, 180) + "...";
        Serial.printf("AdsbLolFetcher: HTTP %d after %lums -- body: %s\n",
                      code, (unsigned long)(millis() - t0), why.c_str());
        http.end();
        return false;
    }

#if defined(BOARD_HAS_PSRAM)
    static PsramAllocator psramAllocator;
    JsonDocument doc(&psramAllocator);
#else
    JsonDocument doc; // no PSRAM: internal RAM, radius-bound — keep it tight
#endif

    String body = http.getString();
    http.end();
    if (body.length() == 0)
    {
        Serial.println("AdsbLolFetcher: empty body");
        return false;
    }

    DeserializationError err = deserializeJson(doc, body);
    if (err)
    {
        Serial.printf("AdsbLolFetcher: JSON parse error: %s\n", err.c_str());
        return false;
    }

    JsonArray ac = doc["ac"].as<JsonArray>();
    if (ac.isNull())
    {
        Serial.println("AdsbLolFetcher: no 'ac' array");
        return false;
    }

    for (JsonObject a : ac)
    {
        if (outStateVectors.size() >= kMaxFlights)
            break;

        // Position, identity, metrics, type and category: shared with the
        // local-receiver source, which reads the same row shape.
        StateVector s;
        if (!readsbAircraftToState(a, s))
            continue;

        // Precomputed by the source, in nm/degrees from the query point.
        double dstNm = optNum(a, "dst");
        s.distance_km = isnan(dstNm) ? haversineKm(centerLat, centerLon, s.lat, s.lon)
                                     : dstNm * kNmToKm;
        double dirDeg = optNum(a, "dir");
        s.bearing_deg = isnan(dirDeg) ? computeBearingDeg(centerLat, centerLon, s.lat, s.lon)
                                      : dirDeg;

        outStateVectors.push_back(s);
    }

    Serial.printf("[fetch] adsb.lol: %u flights in radius\n", (unsigned)outStateVectors.size());
    return true;
}
