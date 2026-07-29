#pragma once

#if defined(ARDUINO) && defined(USE_WIFI)

#include <string>

// Bounded, read-only snapshot used by the authenticated Wi-Fi diagnostics API.
// It intentionally contains no Wi-Fi credentials, pairing secret, or OTA key.
std::string device_diagnostics_json();

#endif
