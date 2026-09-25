#!/usr/bin/env node
//
// Stub of the device's HTTP API, so firmware/data/index.html can be driven in a
// real browser with no board attached.
//
//     node tools/webui_stub.mjs            # http://localhost:8099
//     node tools/webui_stub.mjs 9000       # another port
//     node tools/webui_stub.mjs --export emulator.html --board waveshare
//                                          # ONE self-contained file, no server:
//                                          # open it in any browser (see EXPORT)
//
// The page is read from disk per request, so edits show up on reload. Node
// built-ins only -- no install step.
//
// WHY THIS EXISTS. index.html has no other test surface: it is served from
// LittleFS by a board on a wall, and the only way to exercise it used to be
// `pio run -t uploadfs`, which erases /settings.json. So its behaviour went
// unverified, and four defects accumulated there (F-FW05-A/B/C and F-X05-A in
// the 2026-08-23 audit) -- including a Save button that would persist a blank
// form over live config, and an SSID interpolated unescaped into innerHTML.
// Each was reproduced here first, then fixed, then re-verified. Two of them
// were only fully understood because this harness disagreed with the reading:
// see the "Refresh" note under F-FW05-B below.
//
// ---------------------------------------------------------------------------
// KNOBS. Set as query params on the page URL; they stick until changed.
//
//   ?settingsDelay=N   ms before GET /api/settings answers
//   ?settingsFail=1    GET /api/settings answers 500
//   ?statusDelay=N     ms before GET /api/status answers
//   ?statusFail=1      GET /api/status answers 500
//   ?ssid=...          SSID reported by /api/status
//   ?heap=0            omit largestInternal/largestDma/freeInternal/freePsram,
//                      as firmware from before those were added would
//   ?tz=...            the POSIX TZ string /api/settings reports. Set it to a
//                      zone the page's dropdown does NOT list to exercise the
//                      unlisted-value path (see F-FW05-D below)
//   ?unlisted=1        report a value no dropdown lists for ALL FOUR fields the
//                      firmware stores verbatim -- timezone, noFlightsMode,
//                      panelDriverChip, panelI2sSpeedMhz
//   ?server=URL        the FlightWall server URL /api/settings reports. Point
//                      it at a local `npm start` (e.g. http://localhost:8787)
//                      to drive the page's server cards -- watched flights,
//                      airline names, the code finder -- cross-origin for real
//   ?board=NAME        answer as a FRESH board of that kind: waveshare,
//                      matrixportal or devkit (see BOARDS). Without it the
//                      stub keeps its original DevKit-ish test values
//
// GET /__probe   what the harness saw: POST bodies, request counts, and the
//                peak number of concurrent /api/status requests
// GET /__reset   clear counters (deliberately NOT the in-flight gauge)
//
// ---------------------------------------------------------------------------
// REPRODUCING THE FOUR FINDINGS. Run these in the browser console against the
// page. Each is written as the assertion that FAILS on unfixed code.
//
//   F-FW05-C  security: SSID is attacker-controlled from the RF environment
//     open  /?ssid=%3Cimg%20src%3Dx%20onerror%3D%22window.__XSS%3D1%22%3E
//     then  window.__XSS === 1        // true = the payload executed
//
//   F-FW05-A  Save can persist a blank form over live config
//     open  /?settingsDelay=30000     // long, or tool latency outruns it
//     then  document.getElementById('saveBtn').disabled     // must be true
//           await save(); (await (await fetch('/__probe')).json()).posts
//                                                          // must be []
//     A POST here carries a COMPLETE document of HTML defaults -- blank
//     credentials, panelRes 0x0, brightness 0 -- not a partial one.
//
//   F-X05-A  the page showed only the heap number known to mislead
//     open  /?heap=1   -> expect pills for largest / dma / int / psram
//     open  /?heap=0   -> expect those pills absent, and NOT "undefinedk"
//
//   F-FW05-D  a <select> silently discards a value it has no option for
//     open  /?tz=AEST-10AEDT,M10.1.0,M4.1.0/3     // not in the ten listed
//     then  document.getElementById('timezone').value      // must be that string,
//                                                          // not ""
//           JSON.parse(JSON.stringify(collect())).schedule.timezone
//                                                          // must round-trip
//     Unfixed, .value reads "" and Save POSTs that empty string over a
//     perfectly good zone -- the clock and the night window go with it.
//
//     open  /?unlisted=1                          // all four verbatim fields
//     then  const c = JSON.parse(JSON.stringify(collect()));
//           [c.schedule.timezone, c.layout.noFlightsMode,
//            c.hardware.panelDriverChip, c.hardware.panelI2sSpeedMhz]
//                                                 // none may be "" or 0
//     Settings::fromJson stores those four exactly as sent; the rest of the
//     dropdowns map through a *FromString on the device, so an unlisted value
//     can never come back out of storage for them.
//
//   F-FW05-B  overlapping polls
//     open  /?statusDelay=7000, wait ~25s, read /__probe statusPeak (want 1)
//     then  for (let i=0;i<5;i++) { loadAll(); await new Promise(r=>setTimeout(r,150)); }
//           wait ~22s, read statusPeak again (want 1)
//     The second half is the one that matters: Refresh calls loadStatus
//     directly on top of the timer, so bounding the timer alone still let
//     rapid clicks stack. That only showed up by measuring.
//
// NOTE ON TIMING. Driving this from an agent tool, a round trip can take
// several seconds. Any delay you want to observe must be comfortably longer
// than that, or you will measure the post-load state and conclude the guard is
// missing. Record performance.now() alongside the assertion.
// ---------------------------------------------------------------------------

import { createServer } from 'node:http';
import { readFileSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const PAGE = process.env.WEBUI_PAGE ?? path.join(HERE, '..', 'firmware', 'data', 'index.html');
const ARGS = process.argv.slice(2);
const argAfter = (flag) => { const i = ARGS.indexOf(flag); return i >= 0 ? ARGS[i + 1] : undefined; };
const PORT = Number(ARGS.find((a) => /^\d+$/.test(a)) ?? process.env.PORT ?? 8099);

const knobs = { settingsDelay: 0, settingsFail: 0, statusDelay: 0, statusFail: 0, ssid: 'HomeWiFi', heap: 1, tz: '', unlisted: 0, server: '', board: '' };
knobs.board = argAfter('--board') ?? '';
const probe = { posts: [], settingsGets: 0, statusGets: 0, flightGets: 0, statusInFlight: 0, statusPeak: 0 };

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

/**
 * What GET /api/settings answers. Must stay a superset of every field
 * loadSettings() reads -- see the self-check below, which is not optional
 * decoration: this stub silently drifted from the page twice while the audit
 * fixes were being written, and both times the symptom was loadSettings
 * throwing midway and Save simply never arming. A missing field here looks
 * exactly like the bug under test.
 */
const SETTINGS = {
  // Redacted projection, matching Settings::toJsonPublic(): secrets are
  // replaced by <name>Set booleans. GET /api/settings is unauthenticated.
  network: { wifiSsid: 'HomeWiFi', wifiPasswordSet: true },
  api: {
    openSkyClientId: 'osky-id', openSkyClientSecretSet: true, aeroApiKeySet: false,
    controlTokenSet: false,
    positionSource: 'server', serverUrl: 'https://flightwall.example', receiverUrl: '',
    enrichmentSource: 'adsbdb', enrichmentFallbackToAeroApi: false,
    enrichmentCacheSeconds: 600,
  },
  tracking: {
    centerLat: 40.6413, centerLon: -73.7781, radiusKm: 10, mode: 'flights',
    autoLocateOnBoot: false, trackedFlights: [],
  },
  filters: {
    airlineAllowList: [], airlineDenyList: [], excludeOnGround: true, hideCargo: false,
    showGeneralAviation: false, minAltitudeFt: 0, maxAltitudeFt: 60000,
  },
  display: {
    brightness: 20, maxFlights: 12, cycleSeconds: 3, fetchIntervalSeconds: 30,
    textColorR: 255, textColorG: 255, textColorB: 255,
  },
  layout: { noFlightsMode: 'clockfact' },
  units: { altitude: 'ft', speed: 'mph', climb: 'fps', distance: 'km' },
  schedule: {
    enabled: true, timezone: 'EST5EDT,M3.2.0,M11.1.0', dayBrightness: 20,
    nightBrightness: 5, nightStartHour: 23, nightEndHour: 7,
  },
  light: { enabled: true, type: 'tcs3472', pin: 1, darkThreshold: 500, dimBrightness: 5, dimInstead: true, hysteresis: 30 },
  buttons: { enabled: true },
  hardware: {
    panelResX: 64, panelResY: 64, panelChain: 2, panelRotate180: false, panelClkPhase: true,
    panelDriverChip: 'shift', panelI2sSpeedMhz: 8, panelLatchBlanking: 1,
  },
};

/**
 * What a board answers on first boot, before anyone has saved anything: the
 * defaults in core/Settings.h (and the UserConfiguration / TimingConfiguration
 * values it seeds from), with WiFi already joined so the page is not in its
 * setup-AP state. ?board= and --export start from this, then apply the board's
 * own differences below. The coverage check holds it to the page too.
 */
const FRESH = {
  network: { wifiSsid: 'HomeWiFi', wifiPasswordSet: true },
  api: {
    openSkyClientId: '', openSkyClientSecretSet: false, aeroApiKeySet: false, controlTokenSet: false,
    positionSource: 'opensky', serverUrl: '', receiverUrl: '',
    enrichmentSource: 'adsbdb', enrichmentFallbackToAeroApi: true, enrichmentCacheSeconds: 600,
  },
  tracking: {
    centerLat: 40.6413, centerLon: -73.7781, radiusKm: 10, mode: 'area',
    autoLocateOnBoot: false, trackedFlights: [],
  },
  filters: {
    airlineAllowList: [], airlineDenyList: [], excludeOnGround: true, hideCargo: false,
    showGeneralAviation: false, minAltitudeFt: 0, maxAltitudeFt: 60000,
  },
  display: {
    brightness: 20, maxFlights: 12, cycleSeconds: 3, fetchIntervalSeconds: 30,
    textColorR: 255, textColorG: 255, textColorB: 255,
  },
  layout: { noFlightsMode: 'clockfact' },
  units: { altitude: 'ft', speed: 'mph', climb: 'fps', distance: 'km' },
  schedule: {
    enabled: false, timezone: 'UTC0', dayBrightness: 40,
    nightBrightness: 5, nightStartHour: 22, nightEndHour: 7,
  },
  light: { enabled: true, type: 'tcs3472', pin: 1, darkThreshold: 500, dimBrightness: 3, dimInstead: false, hysteresis: 150 },
  buttons: { enabled: true },
  hardware: {
    panelResX: 64, panelResY: 64, panelChain: 2, panelRotate180: false, panelClkPhase: false,
    panelDriverChip: 'shift', panelI2sSpeedMhz: 20, panelLatchBlanking: 1,
  },
};

/**
 * What differs per board, as the firmware derives it from
 * HardwareConfiguration.h: the pins /api/status publishes (WebConfigServer.cpp
 * builds adc1Pins from isUsableAnalogPin(), so the Waveshare's is EMPTY -- it
 * has no free ADC1 pin -- and its button B is -1, absent), and the settings a
 * fresh board of that kind seeds (the _4x1 envs: 64x64 x 4; Settings.h's
 * panel defaults; each board's LIGHT_DEFAULT_*). Keep in step with those.
 */
const BOARDS = {
  waveshare: {
    name: 'Waveshare ESP32-S3-RGB-Matrix',
    status: {
      i2cSda: 46, i2cScl: 45, adc1Min: 1, adc1Max: 0, adc1Pins: [], buttonAPin: 0, buttonBPin: -1,
      freePsram: 16650000, // 16MB octal
    },
    settings: {
      light: { enabled: false, type: 'tcs3472', pin: 0, darkThreshold: 500, hysteresis: 150 },
      hardware: { panelChain: 4, panelClkPhase: false, panelI2sSpeedMhz: 20 },
    },
  },
  matrixportal: {
    name: 'Adafruit MatrixPortal S3',
    status: {
      i2cSda: 16, i2cScl: 17, adc1Min: 5, adc1Max: 9, adc1Pins: [5, 9], lightOnboardPin: 5,
      buttonAPin: 6, buttonBPin: 7, freePsram: 2050000, // 2MB quad
    },
    settings: {
      light: { enabled: false, type: 'analog', pin: 5, darkThreshold: 40, hysteresis: 40 },
      hardware: { panelChain: 4, panelClkPhase: false, panelI2sSpeedMhz: 20 },
    },
  },
  devkit: {
    name: 'ESP32-S3-DevKitC-1',
    status: {
      i2cSda: 41, i2cScl: 42, adc1Min: 1, adc1Max: 3, adc1Pins: [1, 2, 3], buttonAPin: 18, buttonBPin: 21,
      freePsram: 8350000, // 8MB octal
    },
    settings: {
      light: { enabled: true, type: 'tcs3472', pin: 1, darkThreshold: 500, hysteresis: 150 },
      hardware: { panelChain: 2, panelClkPhase: false, panelI2sSpeedMhz: 20 },
    },
  },
};

/** One level deep, which is all the settings document has: section -> field. */
const overlay = (base, patch = {}) => {
  const out = { ...base };
  for (const [k, v] of Object.entries(patch)) out[k] = { ...(base[k] ?? {}), ...v };
  return out;
};

/** For --board on the command line: a typo there should stop the run. */
function boardOrExit(name) {
  if (!name || BOARDS[name]) return BOARDS[name];
  console.error(`unknown board '${name}'; one of: ${Object.keys(BOARDS).join(', ')}`);
  process.exit(2);
}

/** GET /api/status, before the board and the ?heap knob are applied. */
const STATUS = {
  apMode: false, wifiConnected: true, ssid: 'HomeWiFi', ip: '192.168.1.42',
  mode: 'flights', flightCount: 7, rssi: -58, serverStale: false, freeHeap: 178000,
  i2cSda: 41, i2cScl: 42, adc1Min: 1, adc1Max: 10,
  buttonAPin: 18, buttonBPin: 21, lightLevel: 240, lightDark: false,
};
// The four WebConfigServer.cpp added because freeHeap alone misled a live
// diagnosis. ?heap=0 omits them, standing in for older firmware.
const HEAP = { largestInternal: 151540, largestDma: 98304, freeInternal: 174000, freePsram: 8350000 };

/** Every `s.<section>.<field>` the page reads must exist above. */
function checkSettingsCoverage() {
  let page;
  try {
    page = readFileSync(PAGE, 'utf8');
  } catch {
    console.error(`cannot read ${PAGE}`);
    process.exit(1);
  }
  // Scan ONLY loadSettings()'s body. Elsewhere in the page `s` is the
  // /api/status object, so a whole-file scan would have to skip unknown
  // sections -- and skipping them means a section deleted from SETTINGS goes
  // unnoticed, which is the drift that actually happened.
  const body = page.match(/async function loadSettings\(\)\s*\{([\s\S]*?)\n\}/);
  if (!body) {
    console.error('cannot find loadSettings() in the page; this check needs updating');
    process.exit(1);
  }
  const missing = new Set();
  for (const [name, doc] of [['SETTINGS', SETTINGS], ['FRESH', FRESH]]) {
    for (const m of body[1].matchAll(/\bs\.([a-zA-Z]+)\.([a-zA-Z0-9]+)/g)) {
      const [, section, field] = m;
      const sec = doc[section];
      if (sec === undefined) missing.add(`${name}: ${section}  (whole section)`);
      else if (!(field in sec)) missing.add(`${name}: ${section}.${field}`);
    }
  }
  if (missing.size) {
    console.error(
      `\nThis stub is missing ${missing.size} field(s) index.html reads:\n  ` +
        [...missing].sort().join('\n  ') +
        `\n\nAdd them there. Without them loadSettings() throws partway and\n` +
        `Save never arms -- which is indistinguishable from the bug under test.\n`,
    );
    process.exit(1);
  }
}

const json = (res, body, code = 200) => {
  res.writeHead(code, { 'Content-Type': 'application/json' });
  res.end(JSON.stringify(body));
};

checkSettingsCoverage();
boardOrExit(knobs.board);

// ---------------------------------------------------------------------------
// EXPORT. The real page with a stand-in device INSIDE it, as one file:
//
//     node tools/webui_stub.mjs --export emulator.html [--board waveshare]
//
// Every fetch('/api/...') the page makes is answered in the browser, from the
// same FRESH + BOARDS data the server above uses, so the file works opened
// straight from disk or from any static host -- nothing to install or run.
// Save really saves, into the viewer's own localStorage, so the form
// round-trips the way it does on a board. Secrets become their <name>Set
// flags and the typed values are thrown away, as Settings::toJsonPublic()
// would do; nothing is sent anywhere. The only absolute URLs the page ever
// fetches are a FlightWall server's, and serverUrl starts empty here.
// ---------------------------------------------------------------------------

/** A few aircraft around the default centre (JFK), for the flight list. */
const DEMO_FLIGHTS = [
  { ident: 'DAL1234', airline: 'Delta Air Lines', aircraft: 'B738', operatorIcao: 'DAL', origin: 'JFK', destination: 'LAX',
    helicopter: false, cargo: false, private: false, distanceKm: 4.2, altitudeFt: 18000, speedKt: 400, headingDeg: 263, verticalRateFpm: -640 },
  { ident: 'JBU615', airline: 'JetBlue Airways', aircraft: 'A21N', operatorIcao: 'JBU', origin: 'JFK', destination: 'SJU',
    helicopter: false, cargo: false, private: false, distanceKm: 6.8, altitudeFt: 9800, speedKt: 290, headingDeg: 142, verticalRateFpm: 2100 },
  { ident: 'BAW117', airline: 'British Airways', aircraft: 'B772', operatorIcao: 'BAW', origin: 'LHR', destination: 'JFK',
    helicopter: false, cargo: false, private: false, distanceKm: 8.9, altitudeFt: 3200, speedKt: 172, headingDeg: 41, verticalRateFpm: -900 },
  { ident: 'N512RG', airline: '', aircraft: 'C172', origin: '', destination: '',
    helicopter: false, cargo: false, private: true, distanceKm: 3.1, altitudeFt: 2400, speedKt: 104, headingDeg: 271, verticalRateFpm: 320 },
];

/** Runs in the BROWSER, first thing in <head>: installs the stand-in device. */
function emulatorDevice(DATA) {
  const KEY = 'flightwall-emulator:' + DATA.board;
  const SECRETS = { network: ['wifiPassword'], api: ['openSkyClientSecret', 'aeroApiKey', 'controlToken'] };
  const clone = (o) => JSON.parse(JSON.stringify(o));
  let settings = clone(DATA.settings);
  try {
    const kept = localStorage.getItem(KEY);
    if (kept) settings = JSON.parse(kept);
  } catch (e) { /* private window or blocked storage: start fresh each load */ }

  function save(posted) {
    const next = clone(settings);
    for (const [section, fields] of Object.entries(posted || {})) {
      if (!fields || typeof fields !== 'object' || Array.isArray(fields)) continue;
      next[section] = Object.assign({}, next[section], fields);
      for (const k of SECRETS[section] || []) {
        if (k in fields) { next[section][k + 'Set'] = !!fields[k]; delete next[section][k]; }
      }
    }
    settings = next;
    try { localStorage.setItem(KEY, JSON.stringify(settings)); } catch (e) { /* kept for this visit only */ }
  }

  const routes = {
    'GET /api/settings': () => settings,
    'POST /api/settings': (body) => { save(JSON.parse(body || '{}')); return { ok: true }; },
    'GET /api/status': () => Object.assign({}, DATA.status, {
      mode: settings.tracking.mode, flightCount: DATA.flights.length,
      activeSource: settings.api.positionSource,
    }),
    'GET /api/flights': () => DATA.flights,
    'GET /api/wifiscan': () => [
      { ssid: 'HomeWiFi', rssi: -58, secure: true },
      { ssid: 'Neighbour-5G', rssi: -77, secure: true },
      { ssid: 'Guest', rssi: -84, secure: false },
    ],
    'GET /api/geolocate': () => ({ ok: true, lat: settings.tracking.centerLat, lon: settings.tracking.centerLon,
      place: 'the emulator (no real lookup)' }),
    'POST /api/restart': () => ({ ok: true }),
    'GET /api/firmware': () => ({ ok: false, running: 'emulator', error: 'the emulator has no update server' }),
    'POST /api/firmware': () => ({ ok: false, error: 'the emulator cannot install firmware' }),
    'POST /api/updateui': () => ({ ok: false, error: 'the emulator has no update server' }),
    'DELETE /api/updateui': () => ({ cleared: false }),
  };

  const realFetch = window.fetch.bind(window);
  window.fetch = async (input, init) => {
    const url = typeof input === 'string' ? input : input.url;
    if (!url.startsWith('/api/')) return realFetch(input, init);
    const key = ((init && init.method) || 'GET').toUpperCase() + ' ' + url.split('?')[0];
    await new Promise((r) => setTimeout(r, 150)); // about a board's round trip on WiFi
    const handler = routes[key];
    if (!handler) return new Response('not emulated', { status: 404 });
    return new Response(JSON.stringify(handler(init && init.body)),
      { status: 200, headers: { 'Content-Type': 'application/json' } });
  };

  // Everything the page guards with confirm() -- restart, firmware install,
  // UI rollback -- is a no-op here, so answer yes. Some hosts (a sandboxed
  // iframe, for one) refuse dialogs and return false unseen, which would
  // otherwise make those buttons look dead.
  window.confirm = () => true;

  window.__emulator = {
    reset() {
      try { localStorage.removeItem(KEY); } catch (e) { /* nothing kept */ }
      location.reload();
    },
  };
}

function exportEmulator(file, boardName) {
  const key = boardName || 'waveshare';
  const board = boardOrExit(key);
  const settings = overlay(FRESH, board.settings);
  const status = {
    ...STATUS, ...HEAP, ...board.status,
    lightLevel: -1, note: '', otaError: '', resetReason: 'power-on', sourceFallback: false,
    panelBrightness: settings.display.brightness, panelOff: false, manualBrightness: -1,
    fwVersion: 'emulator', fwPendingVerify: false, uiSource: 'builtin', uiSha: '',
  };
  const geometry = `${settings.hardware.panelChain}×${settings.hardware.panelResX}×${settings.hardware.panelResY}`;
  // `</` inside a JSON string would close the <script>; the data has none, but
  // this is what makes that true for whatever it holds later.
  const data = JSON.stringify({ board: key, settings, status, flights: DEMO_FLIGHTS }).replace(/<\//g, '<\\/');
  // The header sticks below a phone's status bar where the host pads for it;
  // env() is 0 everywhere else, which is the page's own top:0.
  const shim = `<script>\n(${emulatorDevice.toString()})(${data});\n</script>\n` +
    `<style>header{top:env(safe-area-inset-top,0px)}</style>\n`;
  const bar =
    `<div style="padding:8px 16px;font-size:12px;line-height:1.4;color:var(--muted);background:var(--card);` +
    `border-bottom:1px solid var(--line);display:flex;gap:10px;align-items:center;flex-wrap:wrap">` +
    `<span><b style="color:var(--warn)">Emulator</b> · ${board.name}, ${geometry} panels · ` +
    `Save keeps settings in this browser only; nothing reaches a device.</span>` +
    `<button type="button" onclick="__emulator.reset()" style="margin-left:auto;padding:4px 10px;font-size:12px">` +
    `Reset</button></div>\n`;

  let page = readFileSync(PAGE, 'utf8');
  // The shim goes after <title>, not straight after <head>: that keeps
  // <meta charset> inside the first 1024 bytes, where browsers look for it,
  // and still runs before any of the page's own scripts.
  const swaps = [
    [/<title>[^<]*<\/title>\n/, () => '<title>FlightWall Settings Emulator</title>\n' + shim],
    [/<body[^>]*>\n/, (m) => m + bar],
  ];
  for (const [re, to] of swaps) {
    if (!re.test(page)) {
      console.error(`export: ${re} not found in ${PAGE}; this exporter needs updating`);
      process.exit(1);
    }
    page = page.replace(re, to);
  }
  writeFileSync(file, page);
  console.log(`wrote ${file}: the web UI as a ${board.name} with ${geometry} panels`);
}

const EXPORT = argAfter('--export');
if (EXPORT) {
  exportEmulator(EXPORT, knobs.board);
  process.exit(0);
}

createServer(async (req, res) => {
  const url = new URL(req.url, 'http://x');

  for (const k of Object.keys(knobs)) {
    if (url.searchParams.has(k)) {
      const v = url.searchParams.get(k);
      // A mistyped board in a URL answers 400 and leaves the knob alone; it
      // must not take the server down the way a bad --board does at startup.
      if (k === 'board' && v && !BOARDS[v]) {
        return json(res, { error: `unknown board '${v}'`, boards: Object.keys(BOARDS) }, 400);
      }
      knobs[k] = (k === 'ssid' || k === 'tz' || k === 'server' || k === 'board') ? v : Number(v);
    }
  }

  if (url.pathname === '/__probe') return json(res, { ...probe, knobs });
  if (url.pathname === '/__reset') {
    // Deliberately does NOT clear statusInFlight: zeroing it while a request is
    // still in flight makes the later decrement go negative and statusPeak read
    // 0, which reads as "the fix works" when nothing was measured at all.
    Object.assign(probe, { posts: [], settingsGets: 0, statusGets: 0, flightGets: 0 });
    probe.statusPeak = probe.statusInFlight;
    return json(res, { ok: true });
  }

  if (url.pathname === '/' || url.pathname === '/index.html') {
    res.writeHead(200, { 'Content-Type': 'text/html' });
    return res.end(readFileSync(PAGE, 'utf8'));
  }

  if (url.pathname === '/api/settings') {
    if (req.method === 'POST') {
      let body = '';
      for await (const c of req) body += c;
      probe.posts.push(body);
      return json(res, { ok: true });
    }
    probe.settingsGets++;
    if (knobs.settingsDelay) await sleep(knobs.settingsDelay);
    if (knobs.settingsFail) return json(res, { error: 'stubbed failure' }, 500);
    // Overrides are layered onto a copy, so the rest of the document -- and
    // the coverage check above, which reads SETTINGS itself -- is untouched.
    let out = SETTINGS;
    if (knobs.board) out = overlay(FRESH, BOARDS[knobs.board].settings);
    if (knobs.unlisted) {
      out = {
        ...out,
        schedule: { ...out.schedule, timezone: 'AEST-10AEDT,M10.1.0,M4.1.0/3' },
        layout: { ...out.layout, noFlightsMode: 'aurora' },
        hardware: { ...out.hardware, panelDriverChip: 'fm6047', panelI2sSpeedMhz: 10 },
      };
    }
    if (knobs.tz) out = { ...out, schedule: { ...out.schedule, timezone: knobs.tz } };
    if (knobs.server) out = { ...out, api: { ...out.api, serverUrl: knobs.server } };
    return json(res, out);
  }

  if (url.pathname === '/api/status') {
    probe.statusGets++;
    probe.statusInFlight++;
    probe.statusPeak = Math.max(probe.statusPeak, probe.statusInFlight);
    if (knobs.statusDelay) await sleep(knobs.statusDelay);
    probe.statusInFlight--;
    if (knobs.statusFail) return json(res, { error: 'stubbed failure' }, 500);
    let base = { ...STATUS, ssid: knobs.ssid, ...(knobs.heap ? HEAP : {}) };
    if (knobs.board) {
      const { freePsram, ...pins } = BOARDS[knobs.board].status;
      base = { ...base, ...pins, ...(knobs.heap ? { freePsram } : {}) };
    }
    return json(res, base);
  }

  if (url.pathname === '/api/flights') {
    probe.flightGets++;
    return json(res, [
      {
        ident: 'DAL1234', airline: 'Delta', origin: 'JFK', destination: 'LAX',
        aircraft: 'B738', distanceKm: 4.2, altitudeFt: 18000, speedKt: 400,
        headingDeg: 263, verticalRateFpm: -640,
        // What the "ignore DAL" button beside a live flight reads.
        operatorIcao: 'DAL',
      },
      // Exercises the escaping on the flight list too -- these fields come off
      // the wire from a server the user configures.
      { ident: '<b>XSS</b>', airline: '<i>hax</i>', origin: 'EWR', destination: 'BOS', aircraft: 'E75L', distanceKm: 9.9 },
    ]);
  }

  if (url.pathname === '/api/restart') return json(res, { ok: true });
  if (url.pathname === '/api/wifiscan') return json(res, [{ ssid: 'HomeWiFi', rssi: -58 }]);
  if (url.pathname === '/api/geolocate') return json(res, { ok: false });

  res.writeHead(404);
  res.end('not stubbed');
}).listen(PORT, () => {
  console.log(`web UI stub: http://localhost:${PORT}`);
  console.log(`serving ${path.relative(process.cwd(), PAGE)}`);
});
