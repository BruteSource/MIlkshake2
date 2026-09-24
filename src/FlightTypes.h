// Callsign-prefix -> airline/type lookup, ported verbatim from the Cardputer
// build. Purely a string table; no state.
#pragma once
#include <WString.h>

String lookupFlightType(String callsign);
