// Nearby airport lookup via OpenStreetMap's Overpass API, cached to LittleFS
// so it's only fetched once per home-location setup. Ported from the
// Cardputer build's loadOrFetchAirports().
#pragma once
#include <WString.h>
#include <vector>

namespace airports {

struct Airport {
    float lat;
    float lon;
    String name;
};

// Loads the cache if present; otherwise fetches aerodromes within `maxDelta`
// degrees of the current config::current home location and writes the cache.
// `maxDelta` should be the widest zoom level's delta, so every zoom level's
// airports are covered by one fetch. Safe to call with no WiFi (just leaves
// the list empty).
const std::vector<Airport>& get(float maxDelta);

// Re-fetches from Overpass unconditionally and refreshes the cache — call
// after the home location changes.
void refresh(float maxDelta);

}  // namespace airports
