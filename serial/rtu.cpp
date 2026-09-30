#include "rtu.h"

#include <string.h>

namespace t5000::serial
{
    namespace
    {
        void append_crc(std::vector<uint8_t>& frame)
        {
            const uint16_t crc = crc16(frame.data(), frame.size());
            frame.push_back((uint8_t)(crc & 0xFF));
            frame.push_back((uint8_t)(crc >> 8));
        }

        bool crc_matches(const uint8_t* bytes, size_t covered)
        {
            const uint16_t crc = crc16(bytes, covered);
            return bytes[covered] == (uint8_t)(crc & 0xFF) && bytes[covered + 1] == (uint8_t)(crc >> 8);
        }

        bool all_zero(const uint8_t* bytes, size_t from, size_t to)
        {
            for (size_t i = from; i < to; i++)
                if (bytes[i] != 0)
                    return false;
            return true;
        }

        // A reply to the range query, clean, in the first `length` bytes:
        // FF 19 id, and its CRC.
        bool clean_reply(const uint8_t* bytes, size_t length)
        {
            return bytes[0] == kScanAddress && bytes[1] == kScanFunction && crc_matches(bytes, length - 2);
        }
    }

    uint16_t crc16(const uint8_t* data, size_t length)
    {
        uint16_t crc = 0xFFFF;
        for (size_t i = 0; i < length; i++)
        {
            crc ^= data[i];
            for (int bit = 0; bit < 8; bit++)
                crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0xA001) : (uint16_t)(crc >> 1);
        }
        return crc;
    }

    ScanFrame ScanFrame::range_query(uint8_t lo, uint8_t hi)
    {
        ScanFrame f;
        if (lo < kLowestId || hi > kHighestId || lo > hi)
            return f;

        // hi before lo, as T3000 sends them (common.cpp:7231-7232).
        f.m_bytes = { kScanAddress, kScanFunction, hi, lo };
        append_crc(f.m_bytes);
        f.m_lo = lo;
        f.m_hi = hi;
        return f;
    }

    ScanFrame ScanFrame::identity_read(uint8_t id)
    {
        ScanFrame f;
        if (id < kLowestId || id > kHighestId)
            return f;

        f.m_bytes = { id, kReadHoldingFunction, 0, 0, 0, (uint8_t)kIdentityRegisters };
        append_crc(f.m_bytes);
        f.m_lo = id;
        f.m_hi = id;
        return f;
    }

    bool has_mstp_preambles(const uint8_t* bytes, size_t length)
    {
        for (size_t i = 0; i + 9 < length; i++)
        {
            if (bytes[i] == 0x55 && bytes[i + 1] == 0xFF && bytes[i + 8] == 0x55 && bytes[i + 9] == 0xFF)
                return true;
        }
        return false;
    }

    RangeReply decode_range_reply(const uint8_t* reply, size_t length, const ScanFrame& query)
    {
        RangeReply r;

        if (has_mstp_preambles(reply, length))
        {
            r.answer = RangeAnswer::Mstp;
            return r;
        }

        // T3000 reads 13 bytes and judges those. More than that is more than
        // any one device sends.
        uint8_t g[13] = {};
        memcpy(g, reply, length < sizeof(g) ? length : sizeof(g));
        if (length > sizeof(g))
        {
            r.answer = RangeAnswer::Several;
            return r;
        }

        const std::vector<uint8_t>& q = query.bytes();

        // To a query for one id, a clean reply with a byte or two more is
        // noise, not a second device: a second reply is five bytes or more.
        const bool one_id = query.lo() == query.hi();

        if (all_zero(g, 7, 13))
        {
            // Five bytes: FF 19 id, and a CRC over three (common.cpp:7359-7398).
            if (all_zero(g, 0, 5))
                return r;

            // A port with nothing on the far end can hand back what was
            // sent, which is not an answer (common.cpp:7374-7379).
            if (q.size() == 6 && memcmp(g, q.data(), 6) == 0)
                return r;

            if (g[5] != 0 || g[6] != 0)
            {
                // T3000 takes this for two devices (common.cpp:7383-7387).
                r.answer = one_id && clean_reply(g, 5) ? RangeAnswer::Garbled : RangeAnswer::Several;
                return r;
            }
            if (g[0] != kScanAddress || g[1] != kScanFunction || !crc_matches(g, 3))
            {
                r.answer = RangeAnswer::Garbled;
                return r;
            }
        }
        else
        {
            // Nine bytes: FF 19 id, four more, and a CRC over seven
            // (common.cpp:7400-7418).
            if (!all_zero(g, 9, 13))
            {
                // Four bytes at most after a clean reply, since there are
                // 13 in all. T3000 calls this a bus error, to a query for
                // one id (common.cpp:7406-7407). Bytes 0-8 that are not a
                // clean reply are replies run together, such as two
                // five-byte ones.
                r.answer = one_id && clean_reply(g, 9) ? RangeAnswer::Garbled : RangeAnswer::Several;
                return r;
            }
            if (g[0] != kScanAddress || g[1] != kScanFunction || !crc_matches(g, 7))
            {
                r.answer = RangeAnswer::Garbled;
                return r;
            }
        }

        if (g[2] < query.lo() || g[2] > query.hi())
        {
            r.answer = RangeAnswer::Garbled;
            return r;
        }

        r.answer = RangeAnswer::One;
        r.id     = g[2];
        return r;
    }

    bool decode_identity_reply(const uint8_t* reply, size_t length, uint8_t id, DeviceIdentity& out,
                               std::string& error)
    {
        const size_t expected = 3 + 2 * kIdentityRegisters + 2;

        if (length >= 5 && reply[0] == id && reply[1] == (kReadHoldingFunction | 0x80) && crc_matches(reply, 3))
        {
            error = "the device refused the read, with Modbus exception " + std::to_string(reply[2]);
            return false;
        }
        if (length != expected)
        {
            error = "the reply is " + std::to_string(length) + " bytes, where " + std::to_string(expected) +
                    " were expected";
            return false;
        }
        if (reply[0] != id || reply[1] != kReadHoldingFunction || reply[2] != 2 * kIdentityRegisters)
        {
            error = "the reply is not an answer to this read";
            return false;
        }
        if (!crc_matches(reply, expected - 2))
        {
            error = "the reply's CRC does not match";
            return false;
        }

        uint16_t reg[kIdentityRegisters];
        for (int i = 0; i < kIdentityRegisters; i++)
            reg[i] = (uint16_t)((reply[3 + 2 * i] << 8) | reply[4 + 2 * i]);

        // As T3000 adds them up, register by register, whatever each holds
        // (TStatScanner.cpp:1458).
        DeviceIdentity d;
        d.serial = (uint32_t)reg[0] + (uint32_t)reg[1] * 256u + (uint32_t)reg[2] * 65536u + (uint32_t)reg[3] * 16777216u;
        d.firmware = (reg[4] >= 240 && reg[4] < 250) ? (int)reg[4] : (int)reg[5] * 256 + (int)reg[4];
        d.modbus_id = (uint8_t)reg[6];
        d.product   = (uint8_t)reg[7];
        d.hardware  = (uint8_t)reg[8];

        out = d;
        return true;
    }
}
