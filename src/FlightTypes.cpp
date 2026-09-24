#include "FlightTypes.h"

String lookupFlightType(String callsign) {
    callsign.toUpperCase();
    callsign.trim();

    // Major US Carriers
    if (callsign.startsWith("SWA")) return "Southwest";
    if (callsign.startsWith("AAL")) return "American";
    if (callsign.startsWith("DAL")) return "Delta";
    if (callsign.startsWith("UAL")) return "United";
    if (callsign.startsWith("ASA")) return "Alaska";
    if (callsign.startsWith("JBU")) return "JetBlue";
    if (callsign.startsWith("FFT")) return "Frontier";
    if (callsign.startsWith("NKS")) return "Spirit";
    if (callsign.startsWith("HAL")) return "Hawaiian";
    if (callsign.startsWith("AAY")) return "Allegiant";
    if (callsign.startsWith("SCX")) return "Sun Country";

    // Regional Carriers
    if (callsign.startsWith("SKW")) return "SkyWest";
    if (callsign.startsWith("ENY")) return "Envoy Air";
    if (callsign.startsWith("EDV")) return "Endeavor";
    if (callsign.startsWith("RPA")) return "Republic";
    if (callsign.startsWith("JIA")) return "PSA Airlines";
    if (callsign.startsWith("PDT")) return "Piedmont";
    if (callsign.startsWith("GJS")) return "GoJet";
    if (callsign.startsWith("QXE")) return "Horizon";
    if (callsign.startsWith("CPZ")) return "Compass";

    // Cargo Carriers
    if (callsign.startsWith("UPS")) return "UPS Cargo";
    if (callsign.startsWith("FDX")) return "FedEx";
    if (callsign.startsWith("ABX")) return "ABX Air";
    if (callsign.startsWith("ATN")) return "Air Transport";
    if (callsign.startsWith("CJT")) return "Cargojet";
    if (callsign.startsWith("CLX")) return "Cargolux";
    if (callsign.startsWith("GTI")) return "Atlas Air";
    if (callsign.startsWith("FXI")) return "Raya Airways";

    // International Carriers
    if (callsign.startsWith("VOI")) return "Volaris";
    if (callsign.startsWith("AMX")) return "Aeromexico";
    if (callsign.startsWith("CMP")) return "Copa";
    if (callsign.startsWith("ACA")) return "Air Canada";
    if (callsign.startsWith("WJA")) return "WestJet";
    if (callsign.startsWith("BAW")) return "British Air";
    if (callsign.startsWith("DLH")) return "Lufthansa";
    if (callsign.startsWith("AFR")) return "Air France";
    if (callsign.startsWith("KLM")) return "KLM";
    if (callsign.startsWith("UAE")) return "Emirates";
    if (callsign.startsWith("QTR")) return "Qatar";
    if (callsign.startsWith("SIA")) return "Singapore";
    if (callsign.startsWith("CPA")) return "Cathay";
    if (callsign.startsWith("JAL")) return "Japan Air";
    if (callsign.startsWith("ANA")) return "All Nippon";

    // Local / Phoenix Special Entities
    if (callsign.indexOf("POL") >= 0 || callsign.indexOf("PHXPD") >= 0) return "Phx Police";
    if (callsign.indexOf("NEWS") >= 0 || callsign.indexOf("HELO") >= 0) return "News Chopper";

    // Military & Government
    if (callsign.startsWith("REACH") || callsign.startsWith("DUKE") || callsign.startsWith("PAT") ||
        callsign.startsWith("RCH") || callsign.startsWith("CNV") || callsign.startsWith("AZANG") ||
        callsign.startsWith("VMFA") || callsign.startsWith("TMBL") || callsign.startsWith("BDB")) {
        return "Military";
    }

    // General Aviation / Private (Standard US tail numbers)
    if (callsign.startsWith("N") && callsign.length() >= 4 && callsign.length() <= 7) {
        return "Private/GA";
    }

    return "Commercial";
}
