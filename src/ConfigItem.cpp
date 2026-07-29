#include "ConfigItem.h"
#include "Scene.h"
#include "System.h"

std::vector<ConfigItem*> configRequests;

static constexpr uint32_t CONFIG_REQUEST_RETRY_MS = 500;
static constexpr uint8_t  CONFIG_REQUEST_MAX_ATTEMPTS = 4;
static uint32_t           configRequestSentMs     = 0;
static uint8_t            configRequestAttempts   = 0;

static void send_next_config_request() {
    if (configRequests.empty()) {
        return;
    }
    configRequests.front()->send_request();
    configRequestSentMs = millis();
    ++configRequestAttempts;
}

void ConfigItem::init() {
    _known = false;

    for (auto it = configRequests.begin(); it != configRequests.end(); ++it) {
        if (*it == this) {
            configRequests.erase(it);
            break;
        }
    }

    bool start_request = configRequests.empty();
    configRequests.push_back(this);
    if (start_request) {
        configRequestAttempts = 0;
        send_next_config_request();
    }
}

void clear_config_requests() {
    configRequests.clear();
    configRequestSentMs = 0;
    configRequestAttempts = 0;
}

void service_config_requests() {
    if (!configRequests.empty() &&
        (uint32_t)(millis() - configRequestSentMs) >= CONFIG_REQUEST_RETRY_MS) {
        if (configRequestAttempts >= CONFIG_REQUEST_MAX_ATTEMPTS) {
            // A missing or unsupported setting must not monopolize the UART
            // forever. Treat it as the type's safe zero/false default and
            // continue with the remaining discovery requests.
            configRequests.front()->got("");
            configRequests.erase(configRequests.begin());
            configRequestAttempts = 0;
            request_redisplay();
        }
        send_next_config_request();
    }
}

void parse_dollar(const char* line) {
    for (auto it = configRequests.begin(); it != configRequests.end(); ++it) {
        auto item = *it;

        size_t cmdlen = strlen(item->name());

        if (strncmp(line, item->name(), cmdlen) == 0 && line[cmdlen] == '=') {
            line += cmdlen + 1;
            item->got(line);

            request_redisplay();
            configRequests.erase(it);
            configRequestAttempts = 0;
            send_next_config_request();
            break;
        }
    }
}
