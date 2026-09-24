#include "net/OpenSky.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "Secrets.h"

namespace opensky {

static const unsigned long kTokenLifetimeMs = 20UL * 60 * 1000;

static String cachedToken;
static unsigned long tokenFetchedAt = 0;

String token() {
    if (strlen(OPENSKY_CLIENT_ID) == 0 || strlen(OPENSKY_CLIENT_SECRET) == 0) return "";
    if (cachedToken.length() > 0 && (millis() - tokenFetchedAt < kTokenLifetimeMs)) return cachedToken;

    WiFiClientSecure securedClient;
    securedClient.setInsecure();
    HTTPClient http;
    http.begin(securedClient, "https://auth.opensky-network.org/auth/realms/opensky-network/protocol/openid-connect/token");
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    http.useHTTP10(true);

    String body = String("grant_type=client_credentials&client_id=") + OPENSKY_CLIENT_ID +
                  "&client_secret=" + OPENSKY_CLIENT_SECRET;
    int status = http.POST(body);
    String resp = http.getString();

    if (status == 200) {
        JsonDocument doc;
        if (!deserializeJson(doc, resp) && !doc["access_token"].isNull()) {
            cachedToken = doc["access_token"].as<String>();
            tokenFetchedAt = millis();
        }
    }
    http.end();
    return cachedToken;
}

}  // namespace opensky
