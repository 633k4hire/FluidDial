#pragma once

#if defined(ARDUINO) && defined(USE_WIFI)

#include <cstdint>

class WebServer;

// Register secure OTA endpoints on the existing port-80 server. The service is
// available during normal Maijker operation; physical_window is true only when
// the operator deliberately opens More > System > OTA Update.
void secure_ota_register(WebServer& server, bool physical_window);
void secure_ota_set_physical_window(bool open);
void secure_ota_poll();
void secure_ota_stop();
void secure_ota_note_application_healthy();

bool        secure_ota_ready();
bool        secure_ota_update_active();
bool        secure_ota_pairing_pending();
bool        secure_ota_recovery_pending();
const char* secure_ota_pairing_code();
void        secure_ota_confirm_pairing_physical();
void        secure_ota_confirm_recovery_physical();
void        secure_ota_cancel_pairing();

const char* secure_ota_device_id();
const char* secure_ota_device_id_short();
const char* secure_ota_identity_fingerprint();
const char* secure_ota_status();

// Once paired with a configured production trust root, unsigned raw browser
// uploads are disabled. They remain available only for the one-time attended
// bootstrap/recovery path.
bool secure_ota_legacy_upload_allowed();

#endif
