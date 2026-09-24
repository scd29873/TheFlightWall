#pragma once
/*
Purpose: Position source backed by YOUR OWN ADS-B receiver on the LAN --
dump1090-fa (PiAware), readsb or dump1090 -- read from the aircraft.json it
already serves for its own map page.

Why it suits this wall better than the internet sources:
  * Plain HTTP on the local network. No TLS handshake per cycle on a radio the
    panel is known to degrade (HANDOFF.md), no key, no rate limit -- so the
    fetch interval can drop to a few seconds.
  * It shows what your antenna hears, independent of any aggregator.

What it does NOT carry is a route, and dump1090-fa has no aircraft database
either, so enrichment (adsbdb by default) still runs for both, exactly as it
does under OpenSky. readsb with --db-file does fill the type ("t") inline.

g_settings.receiverUrl may be a full aircraft.json URL, or only the receiver's
address ("192.168.1.50", "piaware.local:8080"). For an address, the paths the
common packages serve the file at are tried in turn and the one that answers
is remembered until it stops answering.
*/
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include "interfaces/BaseStateVectorFetcher.h"

class LocalReceiverFetcher : public BaseStateVectorFetcher
{
public:
    LocalReceiverFetcher() = default;
    ~LocalReceiverFetcher() override = default;

    bool fetchStateVectors(double centerLat,
                           double centerLon,
                           double radiusKm,
                           std::vector<StateVector> &outStateVectors) override;

private:
    // When receiverUrl names only an address: the aircraft.json URL found for
    // it, and the receiverUrl it was found for (a changed setting re-searches).
    String m_resolvedFor;
    String m_resolvedUrl;

    WiFiClient m_plain;
    WiFiClientSecure m_secure;
    bool m_secureInit = false;

    // One GET + parse. `httpCode` is the HTTP status, or <= 0 when there was no
    // connection at all -- which tells the path search to stop, since every
    // other path on the same host would fail the same way.
    bool fetchFrom(const String &url, double centerLat, double centerLon, double radiusKm,
                   std::vector<StateVector> &outStateVectors, int &httpCode);
};
