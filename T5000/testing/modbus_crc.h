#pragma once

// CRC-16/MODBUS for the fakes on a serial line: the scan's devices
// (fake_serial_line.h) and the synthetic bootloaders (fake_rtu_bus.h).
//
// Table-driven, where serial::crc16 works bit by bit, so the fakes and the
// code under test cannot share a mistake. The scan's self-test checks this
// one against the standard's check value.

#include <stddef.h>
#include <stdint.h>

#include <array>
#include <vector>

namespace t5000::testing
{
    using SerialBytes = std::vector<uint8_t>;

    inline uint16_t line_crc(const uint8_t* data, size_t length)
    {
        static const std::array<uint16_t, 256> table = [] {
            std::array<uint16_t, 256> t{};
            for (int i = 0; i < 256; i++)
            {
                uint16_t c = (uint16_t)i;
                for (int k = 0; k < 8; k++)
                    c = (c & 1) ? (uint16_t)((c >> 1) ^ 0xA001) : (uint16_t)(c >> 1);
                t[i] = c;
            }
            return t;
        }();

        uint16_t crc = 0xFFFF;
        for (size_t i = 0; i < length; i++)
            crc = (uint16_t)((crc >> 8) ^ table[(crc ^ data[i]) & 0xFF]);
        return crc;
    }

    // Low byte first, as Modbus sends it.
    inline void add_line_crc(SerialBytes& b)
    {
        const uint16_t crc = line_crc(b.data(), b.size());
        b.push_back((uint8_t)(crc & 0xFF));
        b.push_back((uint8_t)(crc >> 8));
    }

    inline bool line_crc_ok(const SerialBytes& b)
    {
        if (b.size() < 3)
            return false;
        const uint16_t crc = line_crc(b.data(), b.size() - 2);
        return b[b.size() - 2] == (uint8_t)(crc & 0xFF) && b[b.size() - 1] == (uint8_t)(crc >> 8);
    }
}
