#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <LovyanGFX.hpp>
#include <vector>

#include "FlightTypes.h"
#include "Secrets.h"
#include "hw/Audio.h"
#include "hw/Display.h"
#include "hw/Storage.h"
#include "hw/Touch.h"
#include "net/OpenSky.h"

static LGFX lcd;
static LGFX_Sprite frame(&lcd);

// Layout Constants
const int SIDEBAR_W = 90;
const int RADAR_W = OT_W - SIDEBAR_W;
const int RADAR_H = OT_H;

// Anti-burn-in: this is an IPS LCD, not OLED, but the sidebar's bright
// static UI in the same spot for hours on end still leaves visible image
// persistence on this panel. Flipping which side it's drawn on periodically
// means no single region of the panel stays lit with the same content
// indefinitely. Purely cosmetic (button positions move with it — handleTap
// and every draw call below compute the current side from this flag rather
// than assuming a fixed layout).
bool sidebarOnRight = false;
unsigned long lastFlipTime = 0;
const unsigned long SIDEBAR_FLIP_INTERVAL = 60000;  // 1 minute

int sidebarOriginX() { return sidebarOnRight ? RADAR_W : 0; }
int radarOriginX() { return sidebarOnRight ? 0 : SIDEBAR_W; }

// Radar UI Colors (Classic Green Phosphor)
const uint16_t RADAR_BG = TFT_BLACK;
const uint16_t RADAR_GRID = TFT_DARKGREEN;
const uint16_t RADAR_BLIP = TFT_GREEN;
const uint16_t RADAR_TEXT = TFT_LIGHTGREY;
const uint16_t UI_BG = 0x18E3;
const uint16_t UI_BTN = 0x2104;
const uint16_t UI_BTN_ACTIVE = 0x0320;
const uint16_t UI_TEXT = TFT_WHITE;

// Zoom / Range Settings (4 levels, matching the Cardputer build)
struct ZoomLevel {
    const char* label;
    float delta;
};
const ZoomLevel ZOOM_LEVELS[4] = {
    {"5M",   0.07f},
    {"10M",  0.15f},
    {"50M",  0.72f},
    {"100M", 1.45f},
};
int currentZoomIdx = 1;

// Refresh interval options (cycled via the sidebar button)
struct RefreshOption {
    const char* label;
    unsigned long ms;
};
const RefreshOption REFRESH_OPTIONS[5] = {
    {"10s", 10000},
    {"15s", 15000},
    {"20s", 20000},
    {"30s", 30000},
    {"1m",  60000},
};
int currentRefreshIdx = 3;
bool showGroundTraffic = false;

unsigned long lastFetchTime = 0;
unsigned long lastTouchTime = 0;
const unsigned long SLEEP_TIMEOUT = 300000;  // 5 minutes of inactivity
bool isSleeping = false;

struct FlightBlip {
    float lat;
    float lon;
    String callsign;
    int altitude;
    int speedKnots;
    int heading;
    bool onGround;
    String flightType;
};
std::vector<FlightBlip> allFetchedFlights;

// Flights currently inside the zoom window — the selectable/drawable set.
std::vector<FlightBlip> visibleTargets;
int selectedTargetIdx = -1;
String selectedTitle = "";

// Forward declarations
void drawRadarScreen();
void updateVisibleTargets();
void fetchFlights();

// Convert Lat/Lon to Screen X/Y Pixels (offset into whichever side the radar
// viewport currently occupies)
void coordsToPixel(float plat, float plon, int &x, int &y) {
    float dLat = plat - HOME_LAT;
    float dLon = plon - HOME_LON;

    float currentDelta = ZOOM_LEVELS[currentZoomIdx].delta;
    float xScale = (RADAR_W / 2.0) / currentDelta;
    float yScale = (RADAR_H / 2.0) / currentDelta;
    yScale *= 1.2;  // Correction factor for non-square Lat/Lon degrees

    x = radarOriginX() + (RADAR_W / 2) + (int)(dLon * xScale);
    y = (RADAR_H / 2) - (int)(dLat * yScale);
}

void drawCompassRose() {
    int cx = radarOriginX() + 20;
    int cy = 20;

    frame.drawCircle(cx, cy, 14, RADAR_GRID);

    frame.setFont(&fonts::Font0);
    frame.setTextColor(RADAR_BLIP);
    frame.setTextDatum(textdatum_t::middle_center);
    frame.drawString("N", cx, cy - 10);
    frame.drawString("S", cx, cy + 10);
    frame.drawString("W", cx - 10, cy);
    frame.drawString("E", cx + 10, cy);
}

void drawRadarGrid() {
    int radarX = radarOriginX();

    frame.fillScreen(RADAR_BG);
    frame.fillRect(radarX, 0, RADAR_W, RADAR_H, RADAR_BG);

    int radarCenterX = radarX + (RADAR_W / 2);
    int radarCenterY = RADAR_H / 2;

    frame.drawLine(radarCenterX, 0, radarCenterX, RADAR_H, RADAR_GRID);
    frame.drawLine(radarX, radarCenterY, radarX + RADAR_W, radarCenterY, RADAR_GRID);

    for (int r = 1; r <= 2; r++) {
        int radius = (RADAR_W / 2) * (r / 2.0);
        for (int d = 0; d < 360; d += 10) {
            float rad = d * 0.0174533;
            int x1 = radarCenterX + cos(rad) * radius;
            int y1 = radarCenterY + sin(rad) * radius;
            int x2 = radarCenterX + cos(rad + 0.08) * radius;
            int y2 = radarCenterY + sin(rad + 0.08) * radius;
            frame.drawLine(x1, y1, x2, y2, RADAR_GRID);
        }
    }

    drawCompassRose();
}

// Sidebar hit-test rects, computed once here and reused by both drawSidebar()
// and the touch handler below so the drawn buttons and their tap targets
// can never drift apart.
struct BtnRect { int16_t x, y, w, h; };
const BtnRect ZOOM_BTN[4] = {
    {6, 20, 39, 20}, {49, 20, 39, 20}, {6, 42, 39, 20}, {49, 42, 39, 20},
};
const BtnRect REFRESH_BTN = {6, 64, 78, 18};
const BtnRect GROUND_BTN = {6, 84, 78, 18};

bool rectContains(const BtnRect& r, int16_t px, int16_t py) {
    return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
}

void drawSidebar() {
    int sx = sidebarOriginX();
    int boundaryX = sidebarOnRight ? sx : sx + SIDEBAR_W;

    frame.fillRect(sx, 0, SIDEBAR_W, OT_H, UI_BG);
    frame.drawFastVLine(boundaryX, 0, OT_H, RADAR_GRID);

    frame.setFont(&fonts::Font2);
    frame.setTextColor(UI_TEXT);
    frame.setTextDatum(textdatum_t::top_center);
    frame.drawString("ZOOM", sx + SIDEBAR_W / 2, 5);

    frame.setFont(&fonts::Font0);
    for (int i = 0; i < 4; i++) {
        const auto& r = ZOOM_BTN[i];
        uint16_t bgCol = (i == currentZoomIdx) ? UI_BTN_ACTIVE : UI_BTN;
        frame.fillRect(sx + r.x, r.y, r.w, r.h, bgCol);
        frame.drawRect(sx + r.x, r.y, r.w, r.h, RADAR_GRID);
        frame.setTextColor(UI_TEXT);
        frame.setTextDatum(textdatum_t::middle_center);
        frame.drawString(ZOOM_LEVELS[i].label, sx + r.x + r.w / 2, r.y + r.h / 2);
    }

    // Refresh / ground / airport controls
    frame.fillRect(sx + REFRESH_BTN.x, REFRESH_BTN.y, REFRESH_BTN.w, REFRESH_BTN.h, UI_BTN);
    frame.drawRect(sx + REFRESH_BTN.x, REFRESH_BTN.y, REFRESH_BTN.w, REFRESH_BTN.h, RADAR_GRID);
    frame.setTextColor(UI_TEXT);
    frame.drawString(String("R:") + REFRESH_OPTIONS[currentRefreshIdx].label,
                     sx + REFRESH_BTN.x + REFRESH_BTN.w / 2, REFRESH_BTN.y + REFRESH_BTN.h / 2);

    frame.fillRect(sx + GROUND_BTN.x, GROUND_BTN.y, GROUND_BTN.w, GROUND_BTN.h,
                  showGroundTraffic ? UI_BTN_ACTIVE : UI_BTN);
    frame.drawRect(sx + GROUND_BTN.x, GROUND_BTN.y, GROUND_BTN.w, GROUND_BTN.h, RADAR_GRID);
    frame.drawString(String("GND:") + (showGroundTraffic ? "ON" : "OFF"),
                     sx + GROUND_BTN.x + GROUND_BTN.w / 2, GROUND_BTN.y + GROUND_BTN.h / 2);

    int infoTopY = 108;
    frame.drawFastHLine(sx + 4, infoTopY - 5, SIDEBAR_W - 8, RADAR_GRID);
    frame.setFont(&fonts::Font2);
    frame.setTextColor(UI_TEXT);
    frame.setTextDatum(textdatum_t::top_center);
    frame.drawString("TARGET", sx + SIDEBAR_W / 2, infoTopY);

    if (selectedTargetIdx >= 0 && selectedTargetIdx < (int)visibleTargets.size()) {
        const auto& t = visibleTargets[selectedTargetIdx];
        frame.setTextDatum(textdatum_t::top_left);
        frame.setFont(&fonts::Font0);
        int textX = sx + 5;
        int cursorY = infoTopY + 18;

        frame.setTextColor(RADAR_BLIP);
        frame.drawString(t.callsign, textX, cursorY);

        cursorY += 13;
        frame.setTextColor(TFT_CYAN);
        frame.drawString(t.flightType, textX, cursorY);
        frame.setTextColor(UI_TEXT);

        cursorY += 13;
        char altStr[20];
        snprintf(altStr, sizeof(altStr), "Alt:%d ft", t.altitude);
        frame.drawString(altStr, textX, cursorY);

        cursorY += 13;
        char spdStr[20];
        snprintf(spdStr, sizeof(spdStr), "Spd:%d kts", t.speedKnots);
        frame.drawString(spdStr, textX, cursorY);

        cursorY += 13;
        char hdgStr[20];
        snprintf(hdgStr, sizeof(hdgStr), "Hdg:%d deg", t.heading);
        frame.drawString(hdgStr, textX, cursorY);

        cursorY += 13;
        frame.setTextColor(t.onGround ? TFT_ORANGE : RADAR_TEXT);
        frame.drawString(t.onGround ? "St:GND" : "St:AIR", textX, cursorY);
    } else {
        frame.setFont(&fonts::Font2);
        frame.setTextColor(RADAR_TEXT);
        frame.setTextDatum(textdatum_t::middle_center);
        frame.drawString("Tap a", sx + SIDEBAR_W / 2, infoTopY + 30);
        frame.drawString("blip", sx + SIDEBAR_W / 2, infoTopY + 45);
    }

    frame.setFont(&fonts::Font2);
    frame.setTextColor(RADAR_TEXT);
    frame.setTextDatum(textdatum_t::bottom_center);
    char countStr[15];
    snprintf(countStr, sizeof(countStr), "Traf:%d", (int)visibleTargets.size());
    frame.drawString(countStr, sx + SIDEBAR_W / 2, OT_H - 5);
}

void updateVisibleTargets() {
    float currentDelta = ZOOM_LEVELS[currentZoomIdx].delta;
    float lamin = HOME_LAT - currentDelta;
    float lamax = HOME_LAT + currentDelta;
    float lomin = HOME_LON - currentDelta;
    float lomax = HOME_LON + currentDelta;

    visibleTargets.clear();

    for (const auto& f : allFetchedFlights) {
        if (!showGroundTraffic && f.onGround) continue;
        if (f.lat >= lamin && f.lat <= lamax && f.lon >= lomin && f.lon <= lomax) {
            visibleTargets.push_back(f);
        }
    }

    selectedTargetIdx = -1;
    if (selectedTitle.length() > 0) {
        for (size_t i = 0; i < visibleTargets.size(); i++) {
            if (visibleTargets[i].callsign == selectedTitle) {
                selectedTargetIdx = (int)i;
                break;
            }
        }
    }
}

void drawRadarScreen() {
    if (isSleeping) return;

    drawRadarGrid();
    int radarX = radarOriginX();

    for (size_t i = 0; i < visibleTargets.size(); i++) {
        const auto& t = visibleTargets[i];
        int px, py;
        coordsToPixel(t.lat, t.lon, px, py);
        if (px >= radarX && px < radarX + RADAR_W && py >= 0 && py < OT_H) {
            uint16_t blipColor = (i == (size_t)selectedTargetIdx) ? TFT_YELLOW
                                                                   : (t.onGround ? TFT_ORANGE : RADAR_BLIP);
            frame.fillCircle(px, py, 2, blipColor);

            float rad = t.heading * 0.01745333f;
            int vectorLen = 10;
            int endX = px + (int)(sin(rad) * vectorLen);
            int endY = py - (int)(cos(rad) * vectorLen);
            frame.drawLine(px, py, endX, endY, blipColor);

            frame.setFont(&fonts::Font0);
            frame.setTextColor(blipColor);
            frame.setTextDatum(textdatum_t::middle_left);
            frame.drawString(t.callsign.c_str(), px + 6, py - 4);
        }
    }

    drawSidebar();

    frame.pushSprite(0, 0);
}

// Parses one flight row's raw text (the content between its own '[' and ']',
// exclusive) into `out`, using only the 7 fields we actually need. Skips a
// short/malformed row rather than guessing.
static void parseStateRow(const String& payload, int start, int end, std::vector<FlightBlip>& out) {
    static constexpr int kFieldsNeeded = 11;  // indices 0..10; we read 1 and 5-10
    String fields[kFieldsNeeded];
    int fieldCount = 0;
    int i = start;
    while (i < end && fieldCount < kFieldsNeeded) {
        while (i < end && payload[i] == ' ') i++;
        String tok;
        if (i < end && payload[i] == '"') {
            i++;
            while (i < end && payload[i] != '"') tok += payload[i++];
            if (i < end) i++;  // closing quote
        } else {
            while (i < end && payload[i] != ',') tok += payload[i++];
        }
        fields[fieldCount++] = tok;
        while (i < end && payload[i] != ',') i++;
        if (i < end) i++;  // comma
    }
    if (fieldCount < kFieldsNeeded) return;  // truncated row — drop it, not a guess

    auto isNull = [](const String& s) { return s.length() == 0 || s == "null"; };

    FlightBlip blip;
    blip.callsign = isNull(fields[1]) ? "N/A" : fields[1];
    blip.callsign.trim();
    blip.lon = isNull(fields[5]) ? 0.0f : fields[5].toFloat();
    blip.lat = isNull(fields[6]) ? 0.0f : fields[6].toFloat();
    blip.altitude = isNull(fields[7]) ? 0 : (int)fields[7].toFloat();
    float velMs = isNull(fields[9]) ? 0.0f : fields[9].toFloat();
    blip.speedKnots = (int)(velMs * 1.94384f);
    blip.heading = isNull(fields[10]) ? 0 : (int)fields[10].toFloat();
    blip.onGround = fields[8] == "true";
    blip.flightType = lookupFlightType(blip.callsign);

    if (blip.lat != 0 && blip.lon != 0) out.push_back(blip);
}

// Hand-rolled parser for OpenSky's flat "states" array — deliberately not
// using ArduinoJson here. Each row has ~17 fields but we only need 7, and
// JsonDocument's per-value slot overhead across ~17 fields x dozens of rows
// (plus its pool growing through several realloc-and-copy steps as it
// parses) was observed to transiently drop free heap to ~2-3KB
// (ESP.getMinFreeHeap()), fragmenting internal RAM badly enough that the
// *next* TLS handshake failed ("SSL - Memory allocation failed") even after
// the free-heap number "recovered". This scans char-by-char with only a
// handful of short-lived String temporaries per row, freed immediately —
// negligible peak footprint by comparison.
//
// `start` is the index right after the array's opening '['. Tracks bracket
// depth so a row's own nested array field (`sensors`, past the fields we
// read) can't be mistaken for a row boundary. A row that never closes
// (response cut short mid-row) is simply never emitted — truncation-safe
// by construction, no separate salvage step needed.
static void parseStatesArray(const String& payload, int start, std::vector<FlightBlip>& out) {
    int n = payload.length();
    int depth = 0;
    int rowStart = -1;
    for (int i = start; i < n; i++) {
        char c = payload[i];
        if (c == '[') {
            if (depth == 0) rowStart = i + 1;
            depth++;
        } else if (c == ']') {
            if (depth == 0) break;  // end of the outer states array
            depth--;
            if (depth == 0) parseStateRow(payload, rowStart, i, out);
        }
    }
}

// Tries to GET+parse one states/all response. Returns true and fills
// `out` only on a clean, fully-parsed response — never partially mutates
// `out` on failure, so a bad attempt can't wipe good data.
static bool tryFetchFlights(const String& url, std::vector<FlightBlip>& out) {
    WiFiClientSecure securedClient;
    securedClient.setInsecure();
    HTTPClient http;
    http.setTimeout(20000);
    http.begin(securedClient, url);
    http.useHTTP10(true);

    String token = opensky::token();
    if (token.length() > 0) http.addHeader("Authorization", "Bearer " + token);

    int httpCode = http.GET();
    if (httpCode != 200) {
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    int statesKey = payload.indexOf("\"states\":");
    if (statesKey < 0) return false;
    int arrayStart = payload.indexOf('[', statesKey);
    if (arrayStart < 0) return false;

    out.clear();
    parseStatesArray(payload, arrayStart + 1, out);
    return true;
}

void fetchFlights() {
    if (WiFi.status() != WL_CONNECTED || isSleeping) return;

    float maxDelta = ZOOM_LEVELS[3].delta;
    String url = "https://opensky-network.org/api/states/all?lamin=" + String(HOME_LAT - maxDelta, 4) +
                 "&lamax=" + String(HOME_LAT + maxDelta, 4) +
                 "&lomin=" + String(HOME_LON - maxDelta, 4) +
                 "&lomax=" + String(HOME_LON + maxDelta, 4);

    // This connection is intermittently flaky (truncated or empty
    // responses, inconsistent byte counts each time) — a handful of
    // retries with a fresh connection rides out almost all of it. On
    // total failure, keep whatever flights were already on screen rather
    // than blanking the radar.
    for (int attempt = 1; attempt <= 3; attempt++) {
        if (tryFetchFlights(url, allFetchedFlights)) break;
        if (attempt < 3) delay(1000);
    }

    updateVisibleTargets();
    drawRadarScreen();
}

// Dispatches a tap at screen coords (x, y) — sidebar buttons on the left,
// blip selection on the radar.
void handleTap(int16_t x, int16_t y) {
    audio::click();

    int sx = sidebarOriginX();
    if (x >= sx && x < sx + SIDEBAR_W) {
        int16_t localX = x - sx;  // ZOOM_BTN/REFRESH_BTN/GROUND_BTN are in sidebar-local coords
        bool handled = false;
        for (int i = 0; i < 4; i++) {
            if (rectContains(ZOOM_BTN[i], localX, y)) {
                if (currentZoomIdx != i) {
                    currentZoomIdx = i;
                    updateVisibleTargets();
                    drawRadarScreen();
                }
                handled = true;
                break;
            }
        }
        if (!handled && rectContains(REFRESH_BTN, localX, y)) {
            currentRefreshIdx = (currentRefreshIdx + 1) % 5;
            lastFetchTime = millis();
            drawRadarScreen();
            handled = true;
        }
        if (!handled && rectContains(GROUND_BTN, localX, y)) {
            showGroundTraffic = !showGroundTraffic;
            updateVisibleTargets();
            drawRadarScreen();
            handled = true;
        }
    } else {
        int closestIdx = -1;
        float minDistance = 25.0f;

        for (size_t i = 0; i < visibleTargets.size(); i++) {
            int px, py;
            coordsToPixel(visibleTargets[i].lat, visibleTargets[i].lon, px, py);

            float dist = sqrt(pow(x - px, 2) + pow(y - py, 2));
            if (dist < minDistance) {
                minDistance = dist;
                closestIdx = (int)i;
            }
        }

        selectedTargetIdx = closestIdx;
        selectedTitle = (closestIdx >= 0) ? visibleTargets[closestIdx].callsign : "";
        drawRadarScreen();
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);

    lcd.init();
    lcd.setRotation(OT_ROTATION);
    lcd.setBrightness(180);

    storage::begin();
    touch::begin();
    audio::begin();
    audio::setLevel(7);

    // This chip has real 8MB octal PSRAM (confirmed via ESP.getPsramSize()
    // under qio_opi — see platformio.ini), so the frame buffer lives there
    // instead of internal SRAM: draws happen in RAM and hit the panel as one
    // batched SPI transfer via pushSprite(), and internal DRAM stays free
    // for WiFi/TLS.
    frame.setColorDepth(16);
    frame.setPsram(true);
    frame.createSprite(OT_W, OT_H);

    // This app never ran touch calibration before, so every tap was mapped
    // through Touch.cpp's generic fallback default — which is mirrored on
    // this specific panel (taps at physical x=90..320 were being reported
    // as x=230..0 or similar, landing every sidebar tap in the radar area
    // instead). Calibrating once here stores a real fit to NVS permanently
    // (touch::begin() loads it back on every future boot), so this only
    // runs the one time.
    if (!touch::isCalibrated()) {
        touch::runCalibration(frame);
    }

    frame.fillScreen(TFT_BLACK);
    frame.setFont(&fonts::Font4);
    frame.setTextColor(TFT_WHITE);
    frame.setTextDatum(textdatum_t::middle_center);
    frame.drawString("CONNECTING...", OT_W / 2, OT_H / 2);
    frame.pushSprite(0, 0);

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    lastTouchTime = millis();
    lastFlipTime = millis();
    fetchFlights();
}

void loop() {
    if (!isSleeping && (millis() - lastTouchTime > SLEEP_TIMEOUT)) {
        isSleeping = true;
        lcd.setBrightness(0);
    }

    if (!isSleeping && (millis() - lastFlipTime > SIDEBAR_FLIP_INTERVAL)) {
        lastFlipTime = millis();
        sidebarOnRight = !sidebarOnRight;
        drawRadarScreen();
    }

    if (!isSleeping && (millis() - lastFetchTime > REFRESH_OPTIONS[currentRefreshIdx].ms)) {
        lastFetchTime = millis();
        fetchFlights();
    }

    auto pt = touch::poll();

    if (pt.pressed) {
        lastTouchTime = millis();

        if (isSleeping) {
            isSleeping = false;
            lcd.setBrightness(180);
            fetchFlights();
            lastFetchTime = millis();
            delay(100);
            return;
        }

        handleTap(pt.x, pt.y);
    }

    delay(30);
}
