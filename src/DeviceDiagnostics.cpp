#if defined(ARDUINO) && defined(USE_WIFI)

#include "DeviceDiagnostics.h"

#include "BootLog.h"
#include "FluidNCModel.h"
#include "LatheModel.h"
#include "System.h"
#include "WiFiConnection.h"

#include <Esp.h>
#include <esp_system.h>

#include <algorithm>
#include <string>

namespace {
    std::string jsonEscape(const char* input) {
        std::string out;
        for (const unsigned char* p =
                 reinterpret_cast<const unsigned char*>(input ? input : "");
             *p;
             ++p) {
            switch (*p) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (*p >= 0x20) out += static_cast<char>(*p);
                    break;
            }
        }
        return out;
    }

    const char* operatorLinkName(OperatorLinkState value) {
        switch (value) {
            case OperatorLinkState::Disconnected: return "disconnected";
            case OperatorLinkState::Synchronizing: return "synchronizing";
            case OperatorLinkState::Ready: return "ready";
            case OperatorLinkState::CommandPending: return "command_pending";
            case OperatorLinkState::Recoverable: return "recoverable";
            case OperatorLinkState::Updating: return "updating";
        }
        return "unknown";
    }
}

std::string device_diagnostics_json() {
    const auto transport = fluidnc_transport_diagnostics();
    const auto wifi      = wifi_connection_diagnostics();
    const auto& link     = fluidnc_link_diagnostics();
    const auto& sync     = lathe_sync_diagnostics();
    const auto& status   = lathe_status();
    const auto& command  = lathe_last_command_result();

    std::string json =
        "{\"schema_version\":1,\"device\":\"m5dial\",\"uptime_ms\":" +
        std::to_string(millis()) + ",\"reset_reason\":" +
        std::to_string(static_cast<int>(esp_reset_reason())) +
        ",\"free_heap\":" + std::to_string(ESP.getFreeHeap()) +
        ",\"min_free_heap\":" + std::to_string(ESP.getMinFreeHeap()) +
        ",\"wifi\":{\"stack_started\":" + (wifi.stack_started ? "true" : "false") +
        ",\"connected\":" + (wifi.connected ? "true" : "false") +
        ",\"reconnect_attempts\":" + std::to_string(wifi.reconnect_attempts) +
        ",\"last_disconnect_reason\":" + std::to_string(wifi.last_disconnect_reason) +
        ",\"association_attempts\":" + std::to_string(wifi.association_attempts) +
        ",\"driver_resets\":" + std::to_string(wifi.driver_resets) +
        ",\"soft_reconnects\":" + std::to_string(wifi.soft_reconnects) +
        ",\"ignored_internal_disconnects\":" + std::to_string(wifi.ignored_internal_disconnects) + "}" +
        ",\"fluidnc\":{\"state\":\"" + jsonEscape(my_state_string) +
        "\",\"state_id\":" + std::to_string(static_cast<int>(state)) +
        ",\"last_alarm\":" + std::to_string(lastAlarm) +
        ",\"last_error\":" + std::to_string(lastError) +
        ",\"operator_link\":\"" + operatorLinkName(operator_link_state()) +
        "\",\"basic_motion_available\":" +
        (operator_basic_motion_actions_available() ? "true" : "false") +
        ",\"strict_actions_available\":" +
        (operator_machine_actions_available() ? "true" : "false") +
        ",\"transport\":{\"kind\":\"uart\",\"baud\":1000000,\"rx_bytes\":" +
        std::to_string(transport.rx_bytes) + ",\"tx_bytes\":" +
        std::to_string(transport.tx_bytes) + ",\"rx_high_water\":" +
        std::to_string(transport.rx_high_water) + ",\"rx_capacity\":4096,\"driver_initializations\":" +
        std::to_string(transport.reinitializations) + ",\"last_rx_ms\":" +
        std::to_string(transport.last_rx_ms) + ",\"last_tx_ms\":" +
        std::to_string(transport.last_tx_ms) + "},\"watchdog\":{\"last_rx_ms\":" +
        std::to_string(link.received_bytes_last_ms) + ",\"timeout_events\":" +
        std::to_string(link.timeout_events) + ",\"recovery_probes\":" +
        std::to_string(link.recovery_probes) + ",\"uart_reinitializations\":" +
        std::to_string(link.uart_reinitializations) + ",\"protocol_errors\":" +
        std::to_string(link.protocol_errors) + ",\"command_timeouts\":" +
        std::to_string(link.command_timeouts) + ",\"last_timeout_ms\":" +
        std::to_string(link.last_timeout_ms) + ",\"last_recovery_ms\":" +
        std::to_string(link.last_recovery_ms) + ",\"consecutive_timeouts\":" +
        std::to_string(link.consecutive_timeouts) + "},\"lathe_sync\":{\"requests\":" +
        std::to_string(sync.requests) + ",\"successful_replies\":" +
        std::to_string(sync.successful_replies) + ",\"failed_replies\":" +
        std::to_string(sync.failed_replies) + ",\"timed_out_replies\":" +
        std::to_string(sync.timed_out_replies) + ",\"recovery_retries\":" +
        std::to_string(sync.recovery_retries) + ",\"last_request_ms\":" +
        std::to_string(sync.last_request_ms) + ",\"last_reply_ms\":" +
        std::to_string(sync.last_reply_ms) + ",\"next_retry_ms\":" +
        std::to_string(sync.next_retry_ms) + ",\"reply_expected\":" +
        (sync.reply_expected ? "true" : "false") + ",\"status_known\":" +
        (status.known ? "true" : "false") + ",\"status_available\":" +
        (status.available ? "true" : "false") + ",\"lathe_enabled\":" +
        (status.enabled ? "true" : "false") + "},\"last_command\":{\"known\":" +
        (command.known ? "true" : "false") + ",\"command\":" +
        std::to_string(command.command) + ",\"pending\":" +
        (command.pending ? "true" : "false") + ",\"recoverable\":" +
        (command.recoverable ? "true" : "false") + ",\"message\":\"" +
        jsonEscape(command.message.c_str()) + "\"}},\"boot_log\":[";

    const int lines = std::min(bootlog_count(), 24);
    for (int i = 0; i < lines; ++i) {
        if (i) json += ",";
        json += "\"" + jsonEscape(bootlog_line(i)) + "\"";
    }
    json += "]}";
    return json;
}

#endif
