// The CRC on a serial scan's frames, against the one T3000's Modbus DLL
// computes, from T3000's own tables.
//
// T5000's serial::crc16 works bit by bit. T3000's CRC16
// (ModbusDllforVc/ModbusDllforVc/crc.cpp:6) looks the bytes up in the two
// tables in crc.h, which are included here as T3000 has them. Its loop is
// copied below, because crc.cpp cannot be compiled on its own: it includes the
// DLL's stdafx.h.
//
// T3000 sends first the byte its CRC16 calls high (common.cpp:7233-7234 for
// the range query, :3734-3736 for a read), and checks replies the same way
// round. That is the low byte of serial::crc16's value. So the checks compare
// frames, byte for byte, rather than the two numbers.
//
// Checked: every input of one and two bytes, and longer ones up to the 255
// bytes CRC16 can take; every range query and identity read a serial scan
// can build, against the frame T3000 builds; and replies carrying T3000's
// CRC, which T5000 must accept.

#include <stdint.h>
#include <string.h>

#include <string>
#include <vector>

#include "../../ModbusDllforVc/ModbusDllforVc/crc.h"
#include "../serial/rtu.h"
#include "../testing/check.h"

namespace
{
    using namespace t5000::serial;
    using namespace t5000::testing;
    using Bytes = std::vector<uint8_t>;

    // crc.cpp:6-18, as T3000 has it.
    unsigned short t3000_crc16(const unsigned char* puchMsg, unsigned char usDataLen)
    {
        unsigned char uchCRCHi = 0xFF;
        unsigned char uchCRCLo = 0xFF;
        unsigned uIndex;
        while (usDataLen--)
        {
            uIndex   = uchCRCHi ^ *puchMsg++;
            uchCRCHi = uchCRCLo ^ auchCRCHi[uIndex];
            uchCRCLo = auchCRCLo[uIndex];
        }
        return (unsigned short)(uchCRCHi << 8 | uchCRCLo);
    }

    // The bytes as T3000 sends them: data, then its CRC16, high byte first.
    Bytes t3000_frame(Bytes data)
    {
        const unsigned short crc = t3000_crc16(data.data(), (unsigned char)data.size());
        data.push_back((uint8_t)((crc >> 8) & 0xFF));
        data.push_back((uint8_t)(crc & 0xFF));
        return data;
    }

    // The same data with T5000's CRC, low byte first.
    Bytes t5000_frame(Bytes data)
    {
        const uint16_t crc = crc16(data.data(), data.size());
        data.push_back((uint8_t)(crc & 0xFF));
        data.push_back((uint8_t)(crc >> 8));
        return data;
    }

    void test_the_copied_loop_is_crc16_modbus()
    {
        section("T3000's CRC16, from its tables, is CRC-16/MODBUS");

        // If the copy of the loop were wrong, the rest of this file would be
        // comparing T5000 with a mistake.
        const Bytes check_string = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };
        const Bytes framed = t3000_frame(check_string);
        check(framed[9] == 0x37 && framed[10] == 0x4B, "\"123456789\" is followed by 37 4B, the check value 0x4B37");

        const Bytes read = t3000_frame({ 0x01, 0x03, 0x00, 0x00, 0x00, 0x0A });
        check(read == Bytes({ 0x01, 0x03, 0x00, 0x00, 0x00, 0x0A, 0xC5, 0xCD }), "01 03 00 00 00 0A is followed by C5 CD");
    }

    void test_every_short_input_agrees()
    {
        section("T5000's CRC agrees with T3000's on every input of one and two bytes");

        int differ = 0;
        for (int a = 0; a < 256; a++)
        {
            if (t5000_frame({ (uint8_t)a }) != t3000_frame({ (uint8_t)a }))
                differ++;
        }
        check_eq(differ, 0, "all 256 one-byte inputs");

        differ = 0;
        for (int a = 0; a < 256; a++)
        {
            for (int b = 0; b < 256; b++)
            {
                if (t5000_frame({ (uint8_t)a, (uint8_t)b }) != t3000_frame({ (uint8_t)a, (uint8_t)b }))
                    differ++;
            }
        }
        check_eq(differ, 0, "all 65536 two-byte inputs");
    }

    void test_longer_inputs_agree()
    {
        section("and on longer inputs, up to the 255 bytes T3000's takes");

        int differ = 0;
        for (int length = 3; length <= 255; length++)
        {
            for (int seed = 0; seed < 8; seed++)
            {
                Bytes data((size_t)length);
                for (int i = 0; i < length; i++)
                    data[(size_t)i] = (uint8_t)(i * 37 + seed * 101 + length);
                if (t5000_frame(data) != t3000_frame(data))
                    differ++;
            }
        }
        check_eq(differ, 0, "2024 inputs of 3-255 bytes");
    }

    void test_every_scan_frame_is_t3000s()
    {
        section("every frame a serial scan can send is the frame T3000 sends");

        // FF 19 hi lo, as common.cpp:7229-7234 builds it.
        int differ = 0;
        int built  = 0;
        for (int lo = kLowestId; lo <= kHighestId; lo++)
        {
            for (int hi = lo; hi <= kHighestId; hi++)
            {
                built++;
                const Bytes t3000 = t3000_frame({ 255, 25, (uint8_t)hi, (uint8_t)lo });
                if (ScanFrame::range_query((uint8_t)lo, (uint8_t)hi).bytes() != t3000)
                    differ++;
            }
        }
        check_eq(built, 254 * 255 / 2, "every range from 1-1 to 254-254");
        check_eq(differ, 0, "each range query is byte for byte T3000's");

        // id 03 00 00 00 0A, as read_multi builds a read of registers 0-9
        // (common.cpp:3727-3736).
        differ = 0;
        for (int id = kLowestId; id <= kHighestId; id++)
        {
            const Bytes t3000 = t3000_frame({ (uint8_t)id, 3, 0, 0, 0, 10 });
            if (ScanFrame::identity_read((uint8_t)id).bytes() != t3000)
                differ++;
        }
        check_eq(differ, 0, "each identity read, ids 1-254, is byte for byte T3000's");
    }

    void test_replies_with_t3000s_crc_are_accepted()
    {
        section("replies carrying T3000's CRC are read");

        const ScanFrame all = ScanFrame::range_query(1, 254);

        // common.cpp:7359-7418: CRC16 over three bytes, or over seven.
        int refused = 0;
        for (int id = kLowestId; id <= kHighestId; id++)
        {
            const Bytes five = t3000_frame({ 255, 25, (uint8_t)id });
            const Bytes nine = t3000_frame({ 255, 25, (uint8_t)id, 0x12, 0x34, 0x56, 0x78 });
            const RangeReply a = decode_range_reply(five.data(), five.size(), all);
            const RangeReply b = decode_range_reply(nine.data(), nine.size(), all);
            if (a.answer != RangeAnswer::One || a.id != id || b.answer != RangeAnswer::One || b.id != id)
                refused++;
        }
        check_eq(refused, 0, "every id's five- and nine-byte reply to a range query");

        // common.cpp:3803: CRC16 over the length*2+3 bytes before it.
        refused = 0;
        for (int id = kLowestId; id <= kHighestId; id++)
        {
            Bytes data = { (uint8_t)id, 3, 20 };
            for (int r = 0; r < kIdentityRegisters; r++)
            {
                data.push_back(0);
                data.push_back((uint8_t)(id + r));
            }
            const Bytes reply = t3000_frame(data);
            DeviceIdentity identity;
            std::string error;
            if (!decode_identity_reply(reply.data(), reply.size(), (uint8_t)id, identity, error))
                refused++;
        }
        check_eq(refused, 0, "every id's reply to a read of registers 0-9");
    }
}

int run_crc_oracle_tests()
{
    test_the_copied_loop_is_crc16_modbus();
    test_every_short_input_agrees();
    test_longer_inputs_agree();
    test_every_scan_frame_is_t3000s();
    test_replies_with_t3000s_crc_are_accepted();
    return 0;
}
