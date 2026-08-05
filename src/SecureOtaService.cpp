#if defined(ARDUINO) && defined(USE_WIFI)

#include "SecureOtaService.h"
#include "DeviceDiagnostics.h"
#include "DiagnosticScreens.h"
#include "LatheModel.h"
#include "System.h"
#include "TamsFirmwarePackage.h"
#include "TamsFirmwareTrust.h"

#include <Esp.h>
#include <Preferences.h>
#include <Update.h>
#include <WebServer.h>
#include <esp_ota_ops.h>
#include <esp_random.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/ecp.h>
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <string>

extern const char* git_info;

namespace {
    constexpr char Namespace[] = "tamsota";
    constexpr char PairLabel[] = "tams-fluiddial-http-pair-v1";
    constexpr char UartPairLabel[] = "tams-fluiddial-uart-pair-v1";
    constexpr uint32_t ChallengeLifetimeMs = 30000;
    constexpr uint32_t SessionTimeoutMs = 45000;
    constexpr uint32_t PairingWindowMs = 120000;
    constexpr uint32_t BootValidationDelayMs = 8000;
    constexpr uint32_t BootValidationTimeoutMs = 30000;
    constexpr size_t MaxChunkBytes = 8192;

    WebServer* server = nullptr;
    bool routesRegistered = false;
    bool serviceReady = false;
    uint32_t physicalWindowUntil = 0;
    uint32_t screenRevision = 0;

    char deviceId[32] = {};
    char deviceIdShort[17] = {};
    char identityFingerprint[65] = {};
    uint8_t identityPrivate[32] = {};
    uint8_t identityPublic[65] = {};
    bool identityReady = false;

    bool paired = false;
    char pairedControllerId[65] = {};
    char pairedControllerFingerprint[65] = {};
    uint8_t pairSecret[32] = {};
    uint32_t lastDeploymentCounter = 0;
    uint32_t acceptedReleaseCounter = 0;

    uint8_t pendingPrivate[32] = {};
    uint8_t pendingPublic[65] = {};
    uint8_t pendingSecret[32] = {};
    char pendingControllerId[65] = {};
    char pendingControllerFingerprint[65] = {};
    char pairingCode[7] = {};
    bool pairingPending = false;
    bool controllerPairConfirmed = false;
    bool recoveryPending = false;
    uint32_t recoveryConfirmedUntil = 0;
    std::string recoveryManifestDigest;
    char uartPairNonce[33] = {};
    bool uartPairAcknowledged = false;

    uint8_t challenge[16] = {};
    char challengeHex[33] = {};
    uint32_t challengeExpires = 0;
    bool challengeLive = false;
    char authenticatedResponseNonce[33] = {};
    uint32_t authenticatedResponseCounter = 0;
    bool authenticatedResponseReady = false;

    struct OtaSession {
        bool active = false;
        char deploymentId[65] = {};
        uint32_t deploymentCounter = 0;
        uint32_t expectedBytes = 0;
        uint32_t writtenBytes = 0;
        uint32_t lastActivity = 0;
        char targetPartition[17] = {};
        TamsFirmware::Manifest manifest;
        std::string manifestDigest;
        mbedtls_sha256_context imageHash;

        OtaSession() { mbedtls_sha256_init(&imageHash); }
        ~OtaSession() { mbedtls_sha256_free(&imageHash); }
    } ota;

    bool bootPendingVerify = false;
    bool applicationHealthy = false;
    uint32_t bootStartedAt = 0;
    char statusText[96] = "initializing";

    void setStatus(const char* text) {
        strncpy(statusText, text ? text : "unknown", sizeof(statusText) - 1);
        statusText[sizeof(statusText) - 1] = '\0';
    }
    void secureZero(void* value, size_t length) {
        volatile uint8_t* p = static_cast<volatile uint8_t*>(value);
        while (length--) *p++ = 0;
    }
    std::string hex(const uint8_t* data, size_t length) {
        static const char chars[] = "0123456789abcdef";
        std::string out(length * 2, '0');
        for (size_t i = 0; i < length; ++i) {
            out[i * 2] = chars[data[i] >> 4];
            out[i * 2 + 1] = chars[data[i] & 0xf];
        }
        return out;
    }
    int nibble(char value) {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        return -1;
    }
    bool unhex(const String& text, uint8_t* output, size_t length) {
        if (text.length() != length * 2) return false;
        for (size_t i = 0; i < length; ++i) {
            int high = nibble(text[i * 2]);
            int low = nibble(text[i * 2 + 1]);
            if (high < 0 || low < 0) return false;
            output[i] = static_cast<uint8_t>((high << 4) | low);
        }
        return true;
    }
    bool unhexVector(const String& text, uint8_t* output, size_t capacity, size_t& length) {
        if ((text.length() & 1) || text.length() / 2 > capacity) return false;
        length = text.length() / 2;
        return unhex(text, output, length);
    }
    std::string jsonEscape(const char* input) {
        std::string out;
        for (const unsigned char* p = reinterpret_cast<const unsigned char*>(input ? input : ""); *p; ++p) {
            if (*p == '"' || *p == '\\') out += '\\';
            if (*p >= 0x20) out += static_cast<char>(*p);
        }
        return out;
    }
    int rng(void*, unsigned char* output, size_t length) {
        esp_fill_random(output, length);
        return 0;
    }
    bool makeKeypair(uint8_t privateKey[32], uint8_t publicKey[65]) {
        mbedtls_ecp_group group;
        mbedtls_mpi d;
        mbedtls_ecp_point q;
        size_t written = 0;
        mbedtls_ecp_group_init(&group);
        mbedtls_mpi_init(&d);
        mbedtls_ecp_point_init(&q);
        bool ok = mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
                  mbedtls_ecp_gen_keypair(&group, &d, &q, rng, nullptr) == 0 &&
                  mbedtls_mpi_write_binary(&d, privateKey, 32) == 0 &&
                  mbedtls_ecp_point_write_binary(&group, &q, MBEDTLS_ECP_PF_UNCOMPRESSED, &written, publicKey, 65) == 0 &&
                  written == 65;
        mbedtls_ecp_point_free(&q);
        mbedtls_mpi_free(&d);
        mbedtls_ecp_group_free(&group);
        if (!ok) {
            secureZero(privateKey, 32);
            secureZero(publicKey, 65);
        }
        return ok;
    }
    bool sharedSecret(const uint8_t privateKey[32], const uint8_t peerPublic[65], uint8_t output[32]) {
        mbedtls_ecp_group group;
        mbedtls_mpi d, z;
        mbedtls_ecp_point q;
        mbedtls_ecp_group_init(&group);
        mbedtls_mpi_init(&d);
        mbedtls_mpi_init(&z);
        mbedtls_ecp_point_init(&q);
        bool ok = mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
                  mbedtls_mpi_read_binary(&d, privateKey, 32) == 0 &&
                  mbedtls_ecp_point_read_binary(&group, &q, peerPublic, 65) == 0 &&
                  mbedtls_ecp_check_pubkey(&group, &q) == 0 &&
                  mbedtls_ecdh_compute_shared(&group, &z, &q, &d, rng, nullptr) == 0 &&
                  mbedtls_mpi_write_binary(&z, output, 32) == 0;
        mbedtls_ecp_point_free(&q);
        mbedtls_mpi_free(&z);
        mbedtls_mpi_free(&d);
        mbedtls_ecp_group_free(&group);
        if (!ok) secureZero(output, 32);
        return ok;
    }
    void sha256(const uint8_t* data, size_t length, uint8_t output[32]) {
        mbedtls_sha256_ret(data, length, output, 0);
    }
    void hmac(const uint8_t* key, size_t keyLength, const uint8_t* data, size_t length, uint8_t output[32]) {
        auto* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
        mbedtls_md_hmac(info, key, keyLength, data, length, output);
    }
    std::string pairDiagnosticTag() {
        if (!paired) return {};
        static const char label[] = "tams-uart-pair-diagnostic-v1";
        uint8_t digest[32];
        hmac(pairSecret,
             sizeof(pairSecret),
             reinterpret_cast<const uint8_t*>(label),
             strlen(label),
             digest);
        std::string value = hex(digest, 8);
        secureZero(digest, sizeof(digest));
        return value;
    }
    bool constantHexEquals(const String& supplied, const uint8_t expected[32]) {
        if (supplied.length() != 64) return false;
        uint8_t actual[32];
        if (!unhex(supplied, actual, sizeof(actual))) return false;
        uint8_t difference = 0;
        for (size_t i = 0; i < sizeof(actual); ++i) difference |= actual[i] ^ expected[i];
        secureZero(actual, sizeof(actual));
        return difference == 0;
    }
    bool physicalWindowOpen() {
        return physicalWindowUntil && static_cast<int32_t>(physicalWindowUntil - millis()) > 0;
    }

    std::string responseField(const char* response, const char* key) {
        if (!response || !key) return {};
        const std::string text(response);
        const std::string prefix(key);
        size_t start = text.find(prefix);
        if (start == std::string::npos ||
            (start != 0 && text[start - 1] != ' ')) return {};
        start += prefix.size();
        size_t end = text.find(' ', start);
        return text.substr(start, end == std::string::npos ? std::string::npos : end - start);
    }

    void rotateUartPairNonce() {
        uint8_t random[16];
        esp_fill_random(random, sizeof(random));
        const std::string value = hex(random, sizeof(random));
        strncpy(uartPairNonce, value.c_str(), sizeof(uartPairNonce) - 1);
        uartPairNonce[sizeof(uartPairNonce) - 1] = '\0';
        secureZero(random, sizeof(random));
        uartPairAcknowledged = false;
    }

    void persistPair(const char* controllerId,
                     const char* controllerFingerprint,
                     const uint8_t secret[32]) {
        Preferences preferences;
        preferences.begin(Namespace, false);
        preferences.putBytes("secret", secret, 32);
        preferences.putString("ctl_id", controllerId);
        preferences.putString("ctl_fp", controllerFingerprint);
        preferences.putBool("paired", true);
        preferences.end();
        memcpy(pairSecret, secret, sizeof(pairSecret));
        strncpy(pairedControllerId, controllerId, sizeof(pairedControllerId) - 1);
        pairedControllerId[sizeof(pairedControllerId) - 1] = '\0';
        strncpy(
            pairedControllerFingerprint,
            controllerFingerprint,
            sizeof(pairedControllerFingerprint) - 1);
        pairedControllerFingerprint[sizeof(pairedControllerFingerprint) - 1] = '\0';
        paired = true;
    }

    const char* fluidNcLinkState() {
        switch (operator_link_state()) {
            case OperatorLinkState::Disconnected: return "disconnected";
            case OperatorLinkState::Synchronizing: return "synchronizing";
            case OperatorLinkState::Ready: return "ready";
            case OperatorLinkState::CommandPending: return "command_pending";
            case OperatorLinkState::Recoverable: return "recoverable";
            case OperatorLinkState::Updating: return "updating";
        }
        return "unknown";
    }

    void loadIdentity() {
        uint64_t mac = ESP.getEfuseMac();
        const char label[] = "tams-fluiddial-device-v1";
        uint8_t digest[32];
        mbedtls_sha256_context context;
        mbedtls_sha256_init(&context);
        mbedtls_sha256_starts_ret(&context, 0);
        mbedtls_sha256_update_ret(&context, reinterpret_cast<const uint8_t*>(label), sizeof(label) - 1);
        mbedtls_sha256_update_ret(&context, reinterpret_cast<const uint8_t*>(&mac), sizeof(mac));
        mbedtls_sha256_finish_ret(&context, digest);
        mbedtls_sha256_free(&context);
        std::string id = "fluiddial-" + hex(digest, 8);
        strncpy(deviceId, id.c_str(), sizeof(deviceId) - 1);
        strncpy(deviceIdShort, id.c_str(), sizeof(deviceIdShort) - 1);

        Preferences preferences;
        preferences.begin(Namespace, false);
        bool haveKey = preferences.getBytesLength("id_priv") == sizeof(identityPrivate) &&
                       preferences.getBytesLength("id_pub") == sizeof(identityPublic);
        if (haveKey) {
            preferences.getBytes("id_priv", identityPrivate, sizeof(identityPrivate));
            preferences.getBytes("id_pub", identityPublic, sizeof(identityPublic));
        } else if (makeKeypair(identityPrivate, identityPublic)) {
            preferences.putBytes("id_priv", identityPrivate, sizeof(identityPrivate));
            preferences.putBytes("id_pub", identityPublic, sizeof(identityPublic));
        }
        identityReady = identityPublic[0] == 0x04;
        sha256(identityPublic, sizeof(identityPublic), digest);
        std::string fingerprint = hex(digest, sizeof(digest));
        strncpy(identityFingerprint, fingerprint.c_str(), sizeof(identityFingerprint) - 1);

        paired = preferences.getBool("paired", false) &&
                 preferences.getBytesLength("secret") == sizeof(pairSecret);
        if (paired) {
            preferences.getBytes("secret", pairSecret, sizeof(pairSecret));
            preferences.getString("ctl_id", "").toCharArray(pairedControllerId, sizeof(pairedControllerId));
            preferences.getString("ctl_fp", "").toCharArray(pairedControllerFingerprint, sizeof(pairedControllerFingerprint));
        }
        lastDeploymentCounter = preferences.getULong("deploy_ctr", 0);
        acceptedReleaseCounter = preferences.getULong("release_ctr", 0);
        preferences.end();
        if (!uartPairNonce[0]) rotateUartPairNonce();
    }

    void clearPendingPair() {
        secureZero(pendingPrivate, sizeof(pendingPrivate));
        secureZero(pendingPublic, sizeof(pendingPublic));
        secureZero(pendingSecret, sizeof(pendingSecret));
        pendingControllerId[0] = pendingControllerFingerprint[0] = pairingCode[0] = '\0';
        pairingPending = controllerPairConfirmed = false;
    }
    void abortOta(const char* reason) {
        if (ota.active) Update.abort();
        ota.active = false;
        ota.deploymentId[0] = '\0';
        ota.expectedBytes = ota.writtenBytes = ota.deploymentCounter = 0;
        ota.targetPartition[0] = '\0';
        ota.manifest = {};
        ota.manifestDigest.clear();
        setStatus(reason);
    }
    void issueChallenge() {
        esp_fill_random(challenge, sizeof(challenge));
        std::string value = hex(challenge, sizeof(challenge));
        strncpy(challengeHex, value.c_str(), sizeof(challengeHex) - 1);
        challengeExpires = millis() + ChallengeLifetimeMs;
        challengeLive = true;
    }
    bool authenticate(const char* method, const char* path, bool beginningDeployment, bool requireActive = true) {
        authenticatedResponseReady = false;
        if (!paired || !server || !challengeLive || static_cast<int32_t>(challengeExpires - millis()) <= 0) return false;
        String target = server->header("X-TAMS-Target");
        String nonce = server->header("X-TAMS-Nonce");
        String counterText = server->header("X-TAMS-Counter");
        String manifest = server->header("X-TAMS-Manifest");
        String bodyHash = server->header("X-TAMS-Body-SHA256");
        String signature = server->header("X-TAMS-Auth");
        if (target != deviceId || nonce != challengeHex || !counterText.length() || manifest.length() != 64 || bodyHash.length() != 64) return false;
        uint32_t counter = strtoul(counterText.c_str(), nullptr, 10);
        if (!counter) return false;
        if (beginningDeployment && counter <= lastDeploymentCounter) return false;
        if (!beginningDeployment && requireActive && (!ota.active || counter != ota.deploymentCounter)) return false;
        if (!beginningDeployment && !requireActive && counter < lastDeploymentCounter) return false;
        std::string canonical = std::string(method) + "\n" + path + "\n" + target.c_str() + "\n" + nonce.c_str() +
                                "\n" + counterText.c_str() + "\n" + manifest.c_str() + "\n" + bodyHash.c_str();
        uint8_t expected[32];
        hmac(pairSecret, sizeof(pairSecret), reinterpret_cast<const uint8_t*>(canonical.data()), canonical.size(), expected);
        bool valid = constantHexEquals(signature, expected);
        secureZero(expected, sizeof(expected));
        challengeLive = false;
        if (valid) {
            strncpy(authenticatedResponseNonce, nonce.c_str(), sizeof(authenticatedResponseNonce) - 1);
            authenticatedResponseCounter = counter;
            authenticatedResponseReady = true;
        }
        return valid;
    }
    bool bodyHashMatches(const String& body) {
        uint8_t digest[32];
        sha256(reinterpret_cast<const uint8_t*>(body.c_str()), body.length(), digest);
        bool valid = server->header("X-TAMS-Body-SHA256") == hex(digest, sizeof(digest)).c_str();
        secureZero(digest, sizeof(digest));
        return valid;
    }
    bool semanticBodyHashMatches(const std::string& body) {
        uint8_t digest[32];
        sha256(reinterpret_cast<const uint8_t*>(body.data()), body.size(), digest);
        bool valid = server->header("X-TAMS-Body-SHA256") == hex(digest, sizeof(digest)).c_str();
        secureZero(digest, sizeof(digest));
        return valid;
    }
    void sendError(int code, const char* error);

    void sendJson(int code, const std::string& json) {
        server->sendHeader("Cache-Control", "no-store");
        if (authenticatedResponseReady) {
            uint8_t bodyDigest[32];
            sha256(reinterpret_cast<const uint8_t*>(json.data()), json.size(), bodyDigest);
            std::string canonical = "response\n" + std::string(authenticatedResponseNonce) + "\n" +
                                    std::to_string(authenticatedResponseCounter) + "\n" +
                                    std::to_string(code) + "\n" + hex(bodyDigest, sizeof(bodyDigest));
            uint8_t proof[32];
            hmac(pairSecret,
                 sizeof(pairSecret),
                 reinterpret_cast<const uint8_t*>(canonical.data()),
                 canonical.size(),
                 proof);
            server->sendHeader("X-TAMS-Response-Auth", hex(proof, sizeof(proof)).c_str());
            secureZero(bodyDigest, sizeof(bodyDigest));
            secureZero(proof, sizeof(proof));
            authenticatedResponseReady = false;
        }
        server->send(code, "application/json", json.c_str());
    }

    void putLe16(uint8_t* destination, uint16_t value) {
        destination[0] = static_cast<uint8_t>(value);
        destination[1] = static_cast<uint8_t>(value >> 8);
    }

    void putLe32(uint8_t* destination, uint32_t value) {
        destination[0] = static_cast<uint8_t>(value);
        destination[1] = static_cast<uint8_t>(value >> 8);
        destination[2] = static_cast<uint8_t>(value >> 16);
        destination[3] = static_cast<uint8_t>(value >> 24);
    }

    constexpr size_t ScreenWidth       = 240;
    constexpr size_t ScreenHeight      = 240;
    constexpr size_t ScreenPixelBytes  = ScreenWidth * ScreenHeight;
    constexpr size_t BmpPaletteBytes   = 256 * 4;
    constexpr size_t BmpHeaderBytes    = 14 + 40 + BmpPaletteBytes;
    constexpr size_t BmpResponseBytes  = BmpHeaderBytes + ScreenPixelBytes;

    void buildScreenBmpHeader(std::array<uint8_t, BmpHeaderBytes>& header) {
        header.fill(0);
        header[0] = 'B';
        header[1] = 'M';
        putLe32(header.data() + 2, BmpResponseBytes);
        putLe32(header.data() + 10, BmpHeaderBytes);
        putLe32(header.data() + 14, 40);
        putLe32(header.data() + 18, ScreenWidth);
        // Negative height makes the RGB332 framebuffer a top-down BMP.
        putLe32(header.data() + 22, static_cast<uint32_t>(-static_cast<int32_t>(ScreenHeight)));
        putLe16(header.data() + 26, 1);
        putLe16(header.data() + 28, 8);
        putLe32(header.data() + 34, ScreenPixelBytes);
        putLe32(header.data() + 46, 256);
        for (size_t index = 0; index < 256; ++index) {
            uint8_t* entry = header.data() + 54 + index * 4;
            entry[0] = static_cast<uint8_t>((index & 0x03) * 255 / 3);
            entry[1] = static_cast<uint8_t>(((index >> 2) & 0x07) * 255 / 7);
            entry[2] = static_cast<uint8_t>(((index >> 5) & 0x07) * 255 / 7);
            entry[3] = 0;
        }
    }

    void attachAuthenticatedBinaryResponse(int code, const uint8_t bodyDigest[32]) {
        if (!authenticatedResponseReady) return;
        std::string canonical = "response\n" + std::string(authenticatedResponseNonce) + "\n" +
                                std::to_string(authenticatedResponseCounter) + "\n" +
                                std::to_string(code) + "\n" + hex(bodyDigest, 32);
        uint8_t proof[32];
        hmac(pairSecret,
             sizeof(pairSecret),
             reinterpret_cast<const uint8_t*>(canonical.data()),
             canonical.size(),
             proof);
        server->sendHeader("X-TAMS-Response-Auth", hex(proof, sizeof(proof)).c_str());
        secureZero(proof, sizeof(proof));
        authenticatedResponseReady = false;
    }

    void handleDiagnostics() {
        constexpr const char* Path = "/api/v1/diagnostics/link";
        if (!paired) return sendError(403, "device is not paired");
        if (!authenticate("GET", Path, false, false)) {
            return sendError(401, "diagnostics authorization failed");
        }
        sendJson(200, device_diagnostics_json());
    }

    void handleScreenCapture() {
        constexpr const char* Path = "/api/v1/diagnostics/screen.bmp";
        if (!paired) return sendError(403, "device is not paired");
        if (!authenticate("GET", Path, false, false)) {
            return sendError(401, "screen capture authorization failed");
        }
        const uint8_t* pixels = static_cast<const uint8_t*>(canvas.getBuffer());
        if (!pixels || canvas.width() != ScreenWidth || canvas.height() != ScreenHeight) {
            return sendError(503, "240x240 RGB332 canvas is unavailable");
        }

        String screenId = server->arg("screen");
        bool previewRendered = false;
        if (screenId.length()) {
            previewRendered = diagnostic_render_screen(screenId.c_str());
            if (!previewRendered) {
                return sendError(422, "unknown diagnostic screen id");
            }
            pixels = static_cast<const uint8_t*>(canvas.getBuffer());
        }

        std::array<uint8_t, BmpHeaderBytes> header;
        buildScreenBmpHeader(header);
        uint8_t bodyDigest[32];
        mbedtls_sha256_context context;
        mbedtls_sha256_init(&context);
        mbedtls_sha256_starts_ret(&context, 0);
        mbedtls_sha256_update_ret(&context, header.data(), header.size());
        mbedtls_sha256_update_ret(&context, pixels, ScreenPixelBytes);
        mbedtls_sha256_finish_ret(&context, bodyDigest);
        mbedtls_sha256_free(&context);

        server->sendHeader("Cache-Control", "no-store");
        if (screenId.length()) {
            server->sendHeader("X-TAMS-Screen-Id", screenId);
            server->sendHeader("X-TAMS-Screen-Revision", String(++screenRevision));
            server->sendHeader("Content-Disposition", "attachment; filename=\"m5dial-" + screenId + ".bmp\"");
        } else {
            server->sendHeader("Content-Disposition", "attachment; filename=\"m5dial-current.bmp\"");
        }
        attachAuthenticatedBinaryResponse(200, bodyDigest);
        secureZero(bodyDigest, sizeof(bodyDigest));
        server->setContentLength(BmpResponseBytes);
        server->send(200, "image/bmp", "");
        server->sendContent(reinterpret_cast<const char*>(header.data()), header.size());
        for (size_t offset = 0; offset < ScreenPixelBytes; offset += 2048) {
            size_t length = std::min<size_t>(2048, ScreenPixelBytes - offset);
            server->sendContent(reinterpret_cast<const char*>(pixels + offset), length);
            delay(0);
        }
        if (previewRendered) {
            diagnostic_restore_screen();
        }
    }
    void sendError(int code, const char* error) {
        sendJson(code, "{\"error\":\"" + jsonEscape(error) + "\"}");
    }

    void handleDevice() {
        std::string json = "{\"product\":\"fluiddial\",\"hardware_role\":\"m5dial_hmi\",\"ota_protocol\":1,\"version\":\"" +
                           jsonEscape(git_info) + "\",\"device_id_short\":\"" + jsonEscape(deviceIdShort) + "\",\"paired\":" +
                           (paired ? "true" : "false") + ",\"trust_configured\":" +
                           (TamsFirmware::trustConfigured() ? "true" : "false") + ",\"uart_pair_acknowledged\":" +
                           (uartPairAcknowledged ? "true" : "false") + ",\"pair_tag\":\"" +
                           jsonEscape(pairDiagnosticTag().c_str()) + "\",\"service_status\":\"" +
                           jsonEscape(statusText) + "\"}";
        sendJson(200, json);
    }
    void handlePairStart() {
        // Pairing is requested from FluidNC.local during normal operation.
        // Opening the bounded window here only permits the comparison flow;
        // the exact device still requires a physical center-button press.
        secure_ota_set_physical_window(true);
        String controllerId = server->arg("controller_id");
        String controllerFingerprint = server->arg("controller_fingerprint");
        String peerHex = server->arg("controller_pub");
        uint8_t peerPublic[65];
        if (!controllerId.length() || controllerId.length() >= sizeof(pendingControllerId) ||
            controllerFingerprint.length() != 64 || !unhex(peerHex, peerPublic, sizeof(peerPublic))) {
            return sendError(400, "invalid pairing request");
        }
        clearPendingPair();
        uint8_t rawShared[32];
        if (!makeKeypair(pendingPrivate, pendingPublic) || !sharedSecret(pendingPrivate, peerPublic, rawShared)) {
            secureZero(peerPublic, sizeof(peerPublic));
            return sendError(500, "ECDH pairing failed");
        }
        std::string transcript = std::string(PairLabel) + "\n" + controllerId.c_str() + "\n" +
                                 controllerFingerprint.c_str() + "\n" + deviceId + "\n" +
                                 identityFingerprint + "\n" + peerHex.c_str() + "\n" +
                                 hex(pendingPublic, sizeof(pendingPublic));
        hmac(rawShared, sizeof(rawShared), reinterpret_cast<const uint8_t*>(transcript.data()), transcript.size(), pendingSecret);
        uint8_t codeDigest[32];
        hmac(pendingSecret, sizeof(pendingSecret), reinterpret_cast<const uint8_t*>("comparison"), 10, codeDigest);
        uint32_t code = (uint32_t(codeDigest[0]) << 16 | uint32_t(codeDigest[1]) << 8 | codeDigest[2]) % 1000000U;
        snprintf(pairingCode, sizeof(pairingCode), "%06lu", static_cast<unsigned long>(code));
        strncpy(pendingControllerId, controllerId.c_str(), sizeof(pendingControllerId) - 1);
        strncpy(pendingControllerFingerprint, controllerFingerprint.c_str(), sizeof(pendingControllerFingerprint) - 1);
        pairingPending = true;
        setStatus("pairing comparison pending");
        secureZero(rawShared, sizeof(rawShared));
        secureZero(codeDigest, sizeof(codeDigest));
        secureZero(peerPublic, sizeof(peerPublic));
        sendJson(200,
                 "{\"device_id\":\"" + std::string(deviceId) + "\",\"identity_fingerprint\":\"" +
                     std::string(identityFingerprint) + "\",\"device_pub\":\"" + hex(pendingPublic, sizeof(pendingPublic)) +
                     "\",\"comparison_code\":\"" + std::string(pairingCode) + "\"}");
    }
    void handlePairControllerConfirm() {
        if (!physicalWindowOpen() || !pairingPending || server->arg("comparison_code") != pairingCode) {
            return sendError(409, "pairing comparison is not pending or does not match");
        }
        controllerPairConfirmed = true;
        setStatus("press center on M5Dial to pair");
        sendJson(202, "{\"status\":\"waiting_for_physical_confirmation\"}");
    }
    void handlePairStatus() {
        sendJson(200,
                 std::string("{\"paired\":") + (paired ? "true" : "false") + ",\"pending\":" +
                     (pairingPending ? "true" : "false") + ",\"controller_confirmed\":" +
                     (controllerPairConfirmed ? "true" : "false") + ",\"device_id\":\"" + deviceId + "\"}");
    }
    void handleChallenge() {
        String controllerId = server->arg("controller_id");
        String clientNonce = server->header("X-TAMS-Client-Nonce");
        String proof = server->header("X-TAMS-Challenge-Auth");
        if (!paired || controllerId != pairedControllerId || clientNonce.length() < 16 || clientNonce.length() > 64) {
            return sendError(403, "controller is not paired");
        }
        std::string canonical = "challenge\n" + std::string(controllerId.c_str()) + "\n" + deviceId + "\n" + clientNonce.c_str();
        uint8_t expected[32];
        hmac(pairSecret, sizeof(pairSecret), reinterpret_cast<const uint8_t*>(canonical.data()), canonical.size(), expected);
        bool proofValid = constantHexEquals(proof, expected);
        secureZero(expected, sizeof(expected));
        if (!proofValid) return sendError(401, "challenge authorization failed");
        issueChallenge();
        sendJson(200,
                 "{\"nonce\":\"" + std::string(challengeHex) + "\",\"next_deployment_counter\":" +
                     std::to_string(lastDeploymentCounter + 1) + ",\"expires_ms\":" +
                     std::to_string(ChallengeLifetimeMs) + "}");
    }
    void handleBegin() {
        String manifestHex = server->arg("manifest");
        String signatureHex = server->arg("signature");
        String deploymentId = server->arg("deployment_id");
        std::string semanticBody = std::string(deploymentId.c_str()) + "\n" + manifestHex.c_str() + "\n" + signatureHex.c_str();
        if (!semanticBodyHashMatches(semanticBody) || !authenticate("POST", "/api/v1/ota/begin", true)) {
            return sendError(401, "OTA authorization failed");
        }
        if (ota.active) return sendError(409, "another deployment is active");
        uint8_t manifest[TamsFirmware::MaxManifestBytes];
        uint8_t signature[TamsFirmware::MaxSignatureBytes];
        size_t manifestLength = 0, signatureLength = 0;
        if (deploymentId.length() < 8 || deploymentId.length() >= sizeof(ota.deploymentId) ||
            !unhexVector(manifestHex, manifest, sizeof(manifest), manifestLength) ||
            !unhexVector(signatureHex, signature, sizeof(signature), signatureLength)) {
            return sendError(400, "invalid signed manifest envelope");
        }
        auto validation = TamsFirmware::validateSignedManifest(manifest, manifestLength, signature, signatureLength);
        std::string compatibilityError;
        bool recoveryDowngrade = validation.manifestValid && validation.signatureValid &&
                                 validation.manifest.recovery && validation.manifest.allowDowngrade &&
                                 validation.manifest.releaseCounter <= acceptedReleaseCounter;
        bool recoveryConfirmed = recoveryDowngrade && recoveryConfirmedUntil &&
                                 !recoveryManifestDigest.empty() &&
                                 recoveryManifestDigest == validation.manifestSha256 &&
                                 static_cast<int32_t>(recoveryConfirmedUntil - millis()) > 0;
        if (recoveryDowngrade && !recoveryConfirmed) {
            if (!physicalWindowOpen()) {
                return sendError(409, "open the M5Dial OTA scene to authorize a recovery downgrade");
            }
            recoveryPending = true;
            recoveryManifestDigest = validation.manifestSha256;
            setStatus("recovery downgrade needs green-button confirmation");
            return sendError(409, "press green on the M5Dial, then retry the signed deployment");
        }
        if (!validation.manifestValid || !validation.signatureValid ||
            server->header("X-TAMS-Manifest") != validation.manifestSha256.c_str() ||
            !TamsFirmware::targetCompatible(validation.manifest,
                                             "fluiddial",
                                             "maijker_m5dial",
                                             "m5dial_hmi",
                                             "esp32s3",
                                             "default_8mb_ab",
                                             1,
                                             acceptedReleaseCounter,
                                             recoveryConfirmed,
                                             compatibilityError)) {
            return sendError(422, validation.error.empty() ? compatibilityError.c_str() : validation.error.c_str());
        }
        const esp_partition_t* inactive = esp_ota_get_next_update_partition(nullptr);
        if (!inactive || validation.manifest.imageLength > inactive->size) return sendError(413, "image does not fit inactive OTA partition");
        if (!Update.begin(validation.manifest.imageLength, U_FLASH)) return sendError(500, "inactive OTA partition could not be opened");
        ota.active = true;
        strncpy(ota.deploymentId, deploymentId.c_str(), sizeof(ota.deploymentId) - 1);
        ota.deploymentCounter = strtoul(server->header("X-TAMS-Counter").c_str(), nullptr, 10);
        ota.expectedBytes = validation.manifest.imageLength;
        ota.writtenBytes = 0;
        ota.lastActivity = millis();
        strncpy(ota.targetPartition, inactive->label, sizeof(ota.targetPartition) - 1);
        ota.manifest = validation.manifest;
        ota.manifestDigest = validation.manifestSha256;
        recoveryPending = false;
        recoveryConfirmedUntil = 0;
        recoveryManifestDigest.clear();
        mbedtls_sha256_starts_ret(&ota.imageHash, 0);
        setStatus("receiving signed image");
        sendJson(200,
                 "{\"deployment_id\":\"" + std::string(ota.deploymentId) + "\",\"accepted_offset\":0,\"max_chunk\":" +
                     std::to_string(MaxChunkBytes) + "}");
    }
    void handleChunk() {
        String body = server->arg("plain");
        if (body.length() > MaxChunkBytes || !bodyHashMatches(body) ||
            !authenticate("PUT", "/api/v1/ota/chunk", false)) return sendError(401, "chunk authorization failed");
        if (!ota.active || server->arg("deployment_id") != ota.deploymentId ||
            server->header("X-TAMS-Manifest") != ota.manifestDigest.c_str()) {
            return sendError(409, "deployment is not active or manifest binding changed");
        }
        uint32_t offset = strtoul(server->arg("offset").c_str(), nullptr, 10);
        if (offset != ota.writtenBytes || ota.writtenBytes + body.length() > ota.expectedBytes) return sendError(409, "chunk offset or size is invalid");
        auto* chunk = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(body.c_str()));
        if (Update.write(chunk, body.length()) != body.length()) {
            abortOta("OTA partition write failed");
            return sendError(500, "OTA partition write failed");
        }
        mbedtls_sha256_update_ret(&ota.imageHash, reinterpret_cast<const uint8_t*>(body.c_str()), body.length());
        ota.writtenBytes += body.length();
        ota.lastActivity = millis();
        sendJson(200, "{\"accepted_offset\":" + std::to_string(ota.writtenBytes) + "}");
    }
    void handleCommit() {
        String body = server->arg("plain");
        if (!bodyHashMatches(body) || !authenticate("POST", "/api/v1/ota/commit", false)) return sendError(401, "commit authorization failed");
        if (!ota.active || ota.writtenBytes != ota.expectedBytes) return sendError(409, "image transfer is incomplete");
        uint8_t digest[32];
        mbedtls_sha256_finish_ret(&ota.imageHash, digest);
        std::string actual = hex(digest, sizeof(digest));
        secureZero(digest, sizeof(digest));
        if (actual != ota.manifest.imageSha256) {
            abortOta("image verification failed");
            return sendError(422, "application image SHA-256 mismatch");
        }
        if (!Update.end(false)) {
            abortOta("ESP image validation failed");
            return sendError(422, "ESP image validation failed");
        }
        Preferences preferences;
        preferences.begin(Namespace, false);
        preferences.putBool("pending", true);
        preferences.putString("pending_id", ota.deploymentId);
        preferences.putString("pending_ver", ota.manifest.version.c_str());
        preferences.putString("pending_hash", ota.manifestDigest.c_str());
        preferences.putULong("pending_rel", ota.manifest.releaseCounter);
        preferences.putULong("pending_ctr", ota.deploymentCounter);
        preferences.putString("pending_part", ota.targetPartition);
        preferences.putString("pending_prev", git_info);
        preferences.end();
        ota.active = false;
        setStatus("verified; rebooting");
        sendJson(200, "{\"status\":\"verified_rebooting\"}");
        delay(250);
        ESP.restart();
    }
    void handleAbort() {
        String body = server->arg("plain");
        if (!bodyHashMatches(body) || !authenticate("POST", "/api/v1/ota/abort", false)) return sendError(401, "abort authorization failed");
        abortOta("deployment aborted");
        sendJson(200, "{\"status\":\"aborted\"}");
    }
    void handleStatus() {
        if (paired && !authenticate("GET", "/api/v1/ota/status", false, !ota.active ? false : true)) {
            return sendError(401, "status authorization failed");
        }
        sendJson(200,
                 "{\"device_id\":\"" + std::string(deviceId) + "\",\"active\":" + (ota.active ? "true" : "false") +
                     ",\"deployment_id\":\"" + jsonEscape(ota.deploymentId) + "\",\"received\":" +
                     std::to_string(ota.writtenBytes) + ",\"expected\":" + std::to_string(ota.expectedBytes) +
                     ",\"status\":\"" + jsonEscape(statusText) + "\"}");
    }
    void handleHealth() {
        if (!paired) return sendError(403, "device is not paired");
        if (!authenticate("GET", "/api/v1/health", false, false)) return sendError(401, "health authorization failed");
        const esp_partition_t* running = esp_ota_get_running_partition();
        esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
        if (running) esp_ota_get_state_partition(running, &state);
        Preferences preferences;
        preferences.begin(Namespace, true);
        std::string lastResult = preferences.getString("last_result", "none").c_str();
        std::string lastDeploymentId = preferences.getString("last_id", "").c_str();
        preferences.end();
        sendJson(200,
                 "{\"device_id\":\"" + std::string(deviceId) + "\",\"identity_fingerprint\":\"" +
                     std::string(identityFingerprint) + "\",\"product\":\"fluiddial\",\"board\":\"maijker_m5dial\","
                     "\"hardware_role\":\"m5dial_hmi\",\"chip\":\"esp32s3\",\"flash_bytes\":" +
                     std::to_string(ESP.getFlashChipSize()) + ",\"version\":\"" + jsonEscape(git_info) +
                     "\",\"release_counter\":" + std::to_string(acceptedReleaseCounter) +
                     ",\"partition\":\"" + (running ? jsonEscape(running->label) : "unknown") +
                     "\",\"ota_state\":" + std::to_string(static_cast<int>(state)) +
                     ",\"paired\":" + (paired ? "true" : "false") + ",\"healthy\":" +
                     (serviceReady && identityReady && applicationHealthy && !bootPendingVerify ? "true" : "false") +
                     ",\"last_deployment_id\":\"" +
                     jsonEscape(lastDeploymentId.c_str()) + "\",\"last_result\":\"" + jsonEscape(lastResult.c_str()) +
                     "\",\"fluidnc_link_state\":\"" + fluidNcLinkState() + "\"}");
    }

    void registerRoutes() {
        if (routesRegistered || !server) return;
        static const char* headers[] = { "X-TAMS-Target", "X-TAMS-Nonce", "X-TAMS-Counter", "X-TAMS-Manifest",
                                         "X-TAMS-Body-SHA256", "X-TAMS-Auth", "X-TAMS-Client-Nonce",
                                         "X-TAMS-Challenge-Auth", "Content-Length" };
        server->collectHeaders(headers, sizeof(headers) / sizeof(headers[0]));
        server->on("/api/v1/device", HTTP_GET, handleDevice);
        server->on("/api/v1/pair/start", HTTP_POST, handlePairStart);
        server->on("/api/v1/pair/confirm", HTTP_POST, handlePairControllerConfirm);
        server->on("/api/v1/pair/status", HTTP_GET, handlePairStatus);
        server->on("/api/v1/challenge", HTTP_POST, handleChallenge);
        server->on("/api/v1/ota/begin", HTTP_POST, handleBegin);
        server->on("/api/v1/ota/chunk", HTTP_PUT, handleChunk);
        server->on("/api/v1/ota/commit", HTTP_POST, handleCommit);
        server->on("/api/v1/ota/status", HTTP_GET, handleStatus);
        server->on("/api/v1/ota/abort", HTTP_POST, handleAbort);
        server->on("/api/v1/health", HTTP_GET, handleHealth);
        server->on("/api/v1/diagnostics/link", HTTP_GET, handleDiagnostics);
        server->on("/api/v1/diagnostics/screen.bmp", HTTP_GET, handleScreenCapture);
        routesRegistered = true;
    }

    void inspectBootState() {
        const esp_partition_t* running = esp_ota_get_running_partition();
        esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
        bootPendingVerify = running && esp_ota_get_state_partition(running, &state) == ESP_OK &&
                            state == ESP_OTA_IMG_PENDING_VERIFY;
        bootStartedAt = millis();
        Preferences preferences;
        preferences.begin(Namespace, false);
        bool pendingRecord = preferences.getBool("pending", false);
        if (pendingRecord && !bootPendingVerify) {
            std::string pendingPartition = preferences.getString("pending_part", "").c_str();
            bool runningPendingPartition = running && !pendingPartition.empty() &&
                                           pendingPartition == running->label;
            if (runningPendingPartition) {
                // Reconcile a power loss after the bootloader marked the image
                // valid but before the NVS deployment record was finalized.
                acceptedReleaseCounter = preferences.getULong("pending_rel", acceptedReleaseCounter);
                lastDeploymentCounter = preferences.getULong("pending_ctr", lastDeploymentCounter);
                preferences.putULong("release_ctr", acceptedReleaseCounter);
                preferences.putULong("deploy_ctr", lastDeploymentCounter);
                preferences.putString("last_result", "success");
                preferences.putString("last_id", preferences.getString("pending_id", ""));
                preferences.putBool("pending", false);
                setStatus("healthy");
            } else {
                preferences.putString("last_result", "rollback");
                preferences.putString("last_id", preferences.getString("pending_id", ""));
                preferences.putBool("pending", false);
                setStatus("rolled back to previous application");
            }
        }
        preferences.end();
        if (bootPendingVerify) setStatus("pending boot health verification");
    }
}

void secure_ota_register(WebServer& httpServer, bool physicalWindow) {
    server = &httpServer;
    if (!identityReady) {
        loadIdentity();
        inspectBootState();
    }
    secure_ota_set_physical_window(physicalWindow);
    registerRoutes();
    serviceReady = identityReady;
    if (!bootPendingVerify) setStatus(serviceReady ? "ready" : "identity initialization failed");
}

void secure_ota_set_physical_window(bool open) {
    physicalWindowUntil = open ? millis() + PairingWindowMs : 0;
    if (!open) {
        clearPendingPair();
        recoveryPending = false;
        recoveryConfirmedUntil = 0;
        recoveryManifestDigest.clear();
    }
}

void secure_ota_poll() {
    if (!identityReady) {
        loadIdentity();
        inspectBootState();
    }
    if (ota.active && static_cast<uint32_t>(millis() - ota.lastActivity) > SessionTimeoutMs) abortOta("deployment timed out");
    if (challengeLive && static_cast<int32_t>(challengeExpires - millis()) <= 0) challengeLive = false;
    if (physicalWindowUntil && !physicalWindowOpen()) {
        physicalWindowUntil = 0;
        clearPendingPair();
        recoveryPending = false;
        recoveryConfirmedUntil = 0;
        recoveryManifestDigest.clear();
    }
    if (bootPendingVerify && serviceReady && identityReady && applicationHealthy &&
        static_cast<uint32_t>(millis() - bootStartedAt) >= BootValidationDelayMs) {
        Preferences preferences;
        preferences.begin(Namespace, false);
        bool pendingRecord = preferences.getBool("pending", false);
        if (esp_ota_mark_app_valid_cancel_rollback() != ESP_OK) {
            preferences.end();
            setStatus("boot self-test failed; rolling back");
            esp_ota_mark_app_invalid_rollback_and_reboot();
            return;
        }
        if (pendingRecord) {
            acceptedReleaseCounter = preferences.getULong("pending_rel", acceptedReleaseCounter);
            lastDeploymentCounter = preferences.getULong("pending_ctr", lastDeploymentCounter);
            preferences.putULong("release_ctr", acceptedReleaseCounter);
            preferences.putULong("deploy_ctr", lastDeploymentCounter);
            preferences.putBool("pending", false);
            preferences.putString("last_result", "success");
            preferences.putString("last_id", preferences.getString("pending_id", ""));
        } else {
            // The attended one-time browser/USB bootstrap has no signed
            // package record. It may establish the secure service only after
            // the same display/NVS/application/OTA self-tests succeed.
            preferences.putString("last_result", "bootstrap_success");
            preferences.putString("last_id", "");
        }
        preferences.end();
        bootPendingVerify = false;
        setStatus("healthy");
    }
    if (bootPendingVerify && (!serviceReady || !identityReady || !applicationHealthy) &&
        static_cast<uint32_t>(millis() - bootStartedAt) >= BootValidationTimeoutMs) {
        setStatus("OTA service self-test timed out; rolling back");
        esp_ota_mark_app_invalid_rollback_and_reboot();
    }
}

void secure_ota_stop() {
    secure_ota_set_physical_window(false);
    serviceReady = false;
}
bool secure_ota_ready() { return serviceReady; }
void secure_ota_note_application_healthy() { applicationHealthy = true; }
bool secure_ota_update_active() { return ota.active; }
bool secure_ota_pairing_pending() { return pairingPending; }
bool secure_ota_recovery_pending() { return recoveryPending; }
const char* secure_ota_pairing_code() { return pairingCode; }
const char* secure_ota_device_id() { return deviceId; }
const char* secure_ota_device_id_short() { return deviceIdShort; }
const char* secure_ota_identity_fingerprint() { return identityFingerprint; }
const char* secure_ota_status() { return statusText; }
bool secure_ota_legacy_upload_allowed() {
    // The installed lathe must remain recoverable when the DLC is unavailable
    // or its pairing record is stale. The operator explicitly chose an
    // unrestricted LAN recovery path, so the classic /update upload remains
    // available even after secure pairing has been established.
    return true;
}

void secure_ota_confirm_pairing_physical() {
    if (!physicalWindowOpen() || !pairingPending || !controllerPairConfirmed) return;
    persistPair(pendingControllerId, pendingControllerFingerprint, pendingSecret);
    clearPendingPair();
    setStatus("paired and ready");
}
void secure_ota_confirm_recovery_physical() {
    if (!physicalWindowOpen() || !recoveryPending || recoveryManifestDigest.empty()) return;
    recoveryPending = false;
    recoveryConfirmedUntil = millis() + PairingWindowMs;
    setStatus("recovery downgrade confirmed; retry from FluidNC.local");
}
void secure_ota_cancel_pairing() {
    clearPendingPair();
    secure_ota_set_physical_window(false);
}

bool secure_ota_uart_pairing_request(char* command, size_t capacity) {
    if (!command || capacity == 0 || !identityReady || uartPairAcknowledged || ota.active) {
        return false;
    }
    if (!uartPairNonce[0]) rotateUartPairNonce();
    const int written = snprintf(
        command,
        capacity,
        "[ESP428]D=%s F=%s N=%s",
        deviceId,
        identityFingerprint,
        uartPairNonce);
    return written > 0 && static_cast<size_t>(written) < capacity;
}

bool secure_ota_accept_uart_pairing_response(const char* response) {
    if (!identityReady || !uartPairNonce[0]) return false;
    const std::string controllerId = responseField(response, "CID=");
    const std::string controllerFingerprint = responseField(response, "CF=");
    const std::string controllerNonce = responseField(response, "CN=");
    uint8_t fingerprintBytes[32];
    uint8_t controllerNonceBytes[16];
    if (controllerId.rfind("fluidnc-", 0) != 0 ||
        controllerId.size() != 24 ||
        !unhex(String(controllerFingerprint.c_str()), fingerprintBytes, sizeof(fingerprintBytes)) ||
        !unhex(String(controllerNonce.c_str()), controllerNonceBytes, sizeof(controllerNonceBytes))) {
        secureZero(fingerprintBytes, sizeof(fingerprintBytes));
        secureZero(controllerNonceBytes, sizeof(controllerNonceBytes));
        return false;
    }

    const std::string transcript =
        std::string(UartPairLabel) + "\n" + controllerId + "\n" +
        controllerFingerprint + "\n" + deviceId + "\n" +
        identityFingerprint + "\n" + uartPairNonce + "\n" + controllerNonce;
    uint8_t derivedSecret[32];
    sha256(
        reinterpret_cast<const uint8_t*>(transcript.data()),
        transcript.size(),
        derivedSecret);
    persistPair(controllerId.c_str(), controllerFingerprint.c_str(), derivedSecret);
    secureZero(derivedSecret, sizeof(derivedSecret));
    secureZero(fingerprintBytes, sizeof(fingerprintBytes));
    secureZero(controllerNonceBytes, sizeof(controllerNonceBytes));
    uartPairAcknowledged = true;
    setStatus("UART-paired and ready");
    return true;
}

void secure_ota_note_uart_link_reset() {
    // Re-advertise the same boot nonce after a controller/link reset. Keeping
    // it stable makes retries idempotent and prevents a transient link flap
    // from racing authenticated Wi-Fi requests against a newly derived secret.
    uartPairAcknowledged = false;
}

#endif
