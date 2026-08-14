// Copyright (c) 2026 FluidDial contributors
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include <cstddef>
#include <cstdint>

namespace FastStateProtocol {
    constexpr uint8_t  SchemaVersion = 1;
    constexpr uint32_t PublishIntervalMs = 50;
    constexpr uint32_t StaleAfterMs = 250;
    constexpr uint32_t LeaseTimeoutMs = 1000;
    constexpr size_t   MaxFrameBytes = 224;

    inline bool sequenceIsNewer(uint32_t candidate, uint32_t current) {
        return candidate != 0 &&
               (current == 0 || static_cast<int32_t>(candidate - current) > 0);
    }

    inline bool timestampFresh(uint32_t now, uint32_t timestamp, uint32_t maximumAgeMs) {
        return timestamp != 0 && static_cast<uint32_t>(now - timestamp) <= maximumAgeMs;
    }

    inline uint16_t crc16(const char* data, size_t length) {
        uint16_t crc = 0xffff;
        for (size_t i = 0; i < length; ++i) {
            crc ^= static_cast<uint16_t>(static_cast<uint8_t>(data[i])) << 8;
            for (uint8_t bit = 0; bit < 8; ++bit) {
                crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                                     : static_cast<uint16_t>(crc << 1);
            }
        }
        return crc;
    }

    inline int hexNibble(char value) {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        return -1;
    }

    inline bool validateAndStrip(char* line) {
        if (line == nullptr) return false;
        size_t length = 0;
        while (line[length] != '\0' && length <= MaxFrameBytes) ++length;
        if (length < 6 || length > MaxFrameBytes || line[length] != '\0') return false;
        const size_t star = length - 5;
        if (line[star] != '*') return false;
        uint16_t supplied = 0;
        for (size_t index = star + 1; index < length; ++index) {
            const int nibble = hexNibble(line[index]);
            if (nibble < 0) return false;
            supplied = static_cast<uint16_t>((supplied << 4) | nibble);
        }
        if (crc16(line, star) != supplied) return false;
        line[star] = '\0';
        return true;
    }
}
