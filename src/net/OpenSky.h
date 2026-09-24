// OpenSky Network OAuth2 client-credentials flow. Ported from the Cardputer
// build — an authenticated token raises OpenSky's request quota substantially
// over the old unauthenticated GreenBox calls. Client ID/secret are optional:
// with none configured, token() always returns "" and callers should fall
// back to an unauthenticated request (still works, just more rate-limited).
#pragma once
#include <WString.h>

namespace opensky {

// Cached for TOKEN_LIFETIME; only re-fetches once expired. "" on failure or
// no client credentials configured.
String token();

}  // namespace opensky
