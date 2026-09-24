#include "net/Airports.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "Secrets.h"
#include "hw/Storage.h"

namespace airports {

static const char* kCachePath = "/airports_cache.json";

static std::vector<Airport> cached;
static bool loaded = false;

static bool loadFromCacheFile() {
    String json;
    if (!storage::loadText(kCachePath, json) || json.length() == 0) return false;

    JsonDocument doc;
    if (deserializeJson(doc, json)) return false;

    cached.clear();
    for (JsonObject obj : doc.as<JsonArray>()) {
        Airport apt;
        apt.lat = obj["lat"] | 0.0f;
        apt.lon = obj["lon"] | 0.0f;
        apt.name = obj["name"] | "Airport";
        if (apt.lat != 0 && apt.lon != 0) cached.push_back(apt);
    }
    return true;
}

// One attempt: POST the query, parse the response into `cached` on success.
// Never touches `cached` on failure, so a bad attempt can't wipe a prior
// good fetch.
static bool tryFetchFromOverpass(const String& query) {
    WiFiClientSecure securedClient;
    securedClient.setInsecure();
    HTTPClient http;
    http.setTimeout(15000);
    http.begin(securedClient, "https://overpass-api.de/api/interpreter");
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    http.addHeader("User-Agent", "GreenBoxFlightTracker/1.0 (ESP32)");
    http.useHTTP10(true);

    int status = http.POST(query);
    String body = http.getString();
    http.end();

    if (status != 200) return false;

    JsonDocument doc;
    if (deserializeJson(doc, body)) return false;

    cached.clear();
    for (JsonObject element : doc["elements"].as<JsonArray>()) {
        float lat = 0, lon = 0;
        if (!element["lat"].isNull() && !element["lon"].isNull()) {
            lat = element["lat"].as<float>();
            lon = element["lon"].as<float>();
        } else if (!element["center"]["lat"].isNull() && !element["center"]["lon"].isNull()) {
            lat = element["center"]["lat"].as<float>();
            lon = element["center"]["lon"].as<float>();
        }

        String name = element["tags"]["name"] | "Airport";
        if (lat != 0 && lon != 0) cached.push_back({lat, lon, name});
    }

    JsonDocument cacheDoc;
    JsonArray arr = cacheDoc.to<JsonArray>();
    for (const auto& apt : cached) {
        JsonObject obj = arr.add<JsonObject>();
        obj["lat"] = apt.lat;
        obj["lon"] = apt.lon;
        obj["name"] = apt.name;
    }
    String out;
    serializeJson(cacheDoc, out);
    storage::saveText(kCachePath, out);
    return true;
}

// Low priority feature: a single attempt only. Airports and flights both
// open fresh TLS connections, and hammering retries back-to-back for both
// in the same boot has been seen to exhaust mbedtls's memory and then take
// the (much higher priority) flights fetch down with it. Just skip airports
// for this boot on failure — the cache fills in whenever one attempt lands.
static void fetchFromOverpass(float maxDelta) {
    float lamin = HOME_LAT - maxDelta;
    float lamax = HOME_LAT + maxDelta;
    float lomin = HOME_LON - maxDelta;
    float lomax = HOME_LON + maxDelta;

    String query = "data=[out:json];nwr[\"aeroway\"=\"aerodrome\"](" +
                   String(lamin, 4) + "," + String(lomin, 4) + "," +
                   String(lamax, 4) + "," + String(lomax, 4) + ");out center;";

    tryFetchFromOverpass(query);
}

const std::vector<Airport>& get(float maxDelta) {
    if (!loaded) {
        loaded = true;
        if (!loadFromCacheFile() && WiFi.status() == WL_CONNECTED) {
            fetchFromOverpass(maxDelta);
        }
    }
    return cached;
}

void refresh(float maxDelta) {
    if (WiFi.status() == WL_CONNECTED) fetchFromOverpass(maxDelta);
    loaded = true;
}

}  // namespace airports
