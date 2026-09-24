// Render the REAL display code (firmware/adapters/Hub75Display.cpp) on the host.
//
// Every screen the wall can show -- flight cards, clock, fun fact, splash -- is
// composed by the unmodified Hub75Display against a stand-in panel (shim/), and
// each frame is written as a raw RGB565 file for render.py to turn into a PNG.
// Built and run by run.sh; see there for usage.
//
// Geometry comes from the same -DFW_PANEL_* flags a firmware env uses, so the
// default here is the MatrixPortal S3 4x1 wall (256x64). Pass a chain length to
// compare another width: `preview <outdir> 2` renders 128x64, `preview <outdir> 2 32`
// renders 128x32.
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <time.h>
#include <vector>

#include "adapters/Hub75Display.h"
#include "core/Settings.h"
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <LittleFS.h>

unsigned long g_fakeMillis = 1000;
HardwareSerial Serial;
LittleFSFS LittleFS;
Settings g_settings;
MatrixPanel_I2S_DMA *g_previewPanel = nullptr;

static std::string g_outDir;

static void dump(const char *name)
{
    const auto &fb = g_previewPanel->frame();
    const std::string path = g_outDir + "/" + name + ".rgb565";
    FILE *f = fopen(path.c_str(), "wb");
    if (!f)
    {
        perror(path.c_str());
        exit(1);
    }
    const uint16_t hdr[2] = {(uint16_t)g_previewPanel->width(), (uint16_t)g_previewPanel->height()};
    fwrite(hdr, sizeof(hdr), 1, f);
    fwrite(fb.data(), sizeof(uint16_t), fb.size(), f);
    fclose(f);
    printf("%s\n", path.c_str());
}

static AirportInfo iata(const char *code)
{
    AirportInfo a;
    a.code_iata = code;
    return a;
}

static FlightInfo airliner(const char *ident, const char *icao, const char *iataOp, const char *name,
                           const char *from, const char *to, const char *type)
{
    FlightInfo f;
    f.ident = ident;
    f.operator_icao = icao;
    f.operator_iata = iataOp;
    f.airline_display_name_full = name;
    if (from[0])
        f.origin = iata(from);
    if (to[0])
        f.destination = iata(to);
    f.aircraft_code = type;
    f.altitude_ft = 35000;
    f.groundspeed_kt = 445;
    f.heading_deg = 92;
    f.vertical_rate_fpm = 0;
    return f;
}

// One card on screen: a single-flight list never cycles.
static void card(Hub75Display &d, const char *name, const FlightInfo &f)
{
    std::vector<FlightInfo> one{f};
    d.markFlightsUpdated();
    d.displayFlights(one);
    dump(name);
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: %s <outdir> [chain] [panel height]\n", argv[0]);
        return 2;
    }
    g_outDir = argv[1];
    if (argc > 2)
        g_settings.panelChain = (uint8_t)atoi(argv[2]);
    if (argc > 3)
        g_settings.panelResY = (uint16_t)atoi(argv[3]);
#ifndef PREVIEW_NO_ROTATE // lets this file build against an upstream tree too
    if (getenv("PREVIEW_ROTATE180"))
        g_settings.panelRotate180 = true;
#endif
    setenv("TZ", "EST5EDT,M3.2.0,M11.1.0", 1);
    tzset();

    Hub75Display d;
    d.initialize();
    printf("# %ux%u, refresh ~%d Hz at lsbMsbTransitionBit %d\n", g_previewPanel->width(),
           g_previewPanel->height(), g_previewPanel->calculated_refresh_rate,
           g_previewPanel->lsbMsbTransitionBit);

    d.displaySplash();
    dump("00-splash");

    FlightInfo ual = airliner("UA1234", "UAL", "UA", "United Airlines", "SFO", "EWR", "B77W");
    ual.vertical_rate_fpm = 640;
    card(d, "01-united", ual);

    FlightInfo baw = airliner("BA181", "BAW", "BA", "British Airways", "LHR", "JFK", "B772");
    baw.pinned = true;
    baw.eta_text = "~1h05";
    baw.progress_pct = 78;
    baw.altitude_ft = 24800;
    baw.vertical_rate_fpm = -1800;
    card(d, "02-tracked", baw);

    FlightInfo dal = airliner("DL2291", "DAL", "DL", "Delta Air Lines", "ATL", "", "A21N");
    dal.altitude_ft = 11200;
    dal.groundspeed_kt = 290;
    card(d, "03-half-route", dal);

    // No bundled tile for this operator: the brand-colour code badge.
    FlightInfo brz = airliner("MX412", "MXY", "MX", "Breeze Airways", "PVD", "CHS", "BCS3");
    brz.operator_icao = "ZZB"; // force the badge path
    card(d, "04-badge", brz);

    FlightInfo ga;
    ga.ident = "N512RG";
    ga.is_private = true;
    ga.aircraft_code = "C172";
    ga.altitude_ft = 2400;
    ga.groundspeed_kt = 104;
    ga.heading_deg = 271;
    ga.vertical_rate_fpm = 320;
    card(d, "05-private", ga);

    // ICAO route codes: the widest route row there is.
    FlightInfo icao = airliner("AC761", "ACA", "AC", "Air Canada", "", "", "A223");
    icao.origin.code_icao = "CYYZ";
    icao.destination.code_icao = "KBOS";
    card(d, "06-icao-route", icao);

    // Empty sky: clock, then a fun fact (no flights -> the no-flights screen).
    std::vector<FlightInfo> none;
    g_settings.layout.noFlightsMode = "clock";
    d.markFlightsUpdated();
    d.displayFlights(none);
    dump("07-clock");

    g_settings.layout.noFlightsMode = "funfact";
    g_fakeMillis = 7000UL * 3; // pick a fact
    d.markFlightsUpdated();
    d.displayFlights(none);
    dump("08-funfact");
    return 0;
}
