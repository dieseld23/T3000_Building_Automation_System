#pragma once

// The Modbus RTU a serial scan speaks: two frames out, and the replies to them.
//
// This is all of it. A serial scan sends Temco's range query and a read of
// the ten identity registers, and ScanFrame has no way to build anything
// else - no constructor takes bytes, and no builder takes a function code. A
// write (functions 5, 6, 15 and 16) cannot be expressed here, so the scan
// cannot send one, however it is changed. T3000's serial scan writes to
// registers 10, 0, 2, 8 and 16 on its own (TStatScanner.cpp:1215, :1463-1485,
// :1657); nothing here can.
//
// The frames and the rules for reading the replies are T3000's, from
// ModbusDllforVc/common.cpp and TStatScanner.cpp, cited at each one.

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

namespace t5000::serial
{
    // CRC-16/MODBUS, sent low byte first. T3000's CRC16
    // (ModbusDllforVc/crc.cpp:6) is the Modicon table version of the same
    // thing: the byte it calls high and sends first is this value's low byte.
    // conformance/crc_oracle.cpp checks the two agree, using T3000's tables.
    uint16_t crc16(const uint8_t* data, size_t length);

    // The range query goes to address 255, which every Temco device on the
    // line answers if its id is in the range (common.cpp:7229-7235).
    inline constexpr uint8_t kScanAddress = 255;
    inline constexpr uint8_t kScanFunction = 0x19;

    // Function 3, read holding registers.
    inline constexpr uint8_t kReadHoldingFunction = 0x03;

    // The ids a scan covers (common.cpp:7221 refuses anything outside them).
    inline constexpr uint8_t kLowestId  = 1;
    inline constexpr uint8_t kHighestId = 254;

    // Registers 0-9, which T3000 reads from every id its scan finds
    // (TStatScanner.cpp:1432): serial, firmware, id, product, hardware.
    inline constexpr int kIdentityRegisters = 10;

    // A frame a serial scan may send. There are two kinds, and these two
    // functions are the only way to make one.
    class ScanFrame
    {
    public:
        // FF 19 hi lo, then the CRC. Asks every device with an id from lo to
        // hi to answer. Empty, and not sendable, unless
        // 1 <= lo <= hi <= 254.
        static ScanFrame range_query(uint8_t lo, uint8_t hi);

        // id 03 00 00 00 0A, then the CRC: registers 0-9 of one id. Empty,
        // and not sendable, unless 1 <= id <= 254.
        static ScanFrame identity_read(uint8_t id);

        const std::vector<uint8_t>& bytes() const { return m_bytes; }
        bool empty() const { return m_bytes.empty(); }

        // The range a query asks about, or the id a read asks. 0 for an
        // empty frame.
        uint8_t lo() const { return m_lo; }
        uint8_t hi() const { return m_hi; }

    private:
        ScanFrame() = default;

        std::vector<uint8_t> m_bytes;
        uint8_t m_lo = 0;
        uint8_t m_hi = 0;
    };

    // What came back to a range query.
    enum class RangeAnswer
    {
        Nobody,    // nothing, or our own query echoed by a port with nothing on it
        One,       // one device, clean
        Several,   // more replies than one device sends
        Garbled,   // one reply's worth that does not check out
        Mstp,      // the line is running BACnet MS/TP
    };

    struct RangeReply
    {
        RangeAnswer answer = RangeAnswer::Nobody;
        uint8_t     id     = 0;   // when One
    };

    // Reads a reply to `query` the way CheckTstatOnline2_a_nocretical does
    // (common.cpp:7330-7433), looking at the first 13 bytes as it does:
    //
    //   - MS/TP preambles anywhere: Mstp.
    //   - All zero, or the query itself: Nobody.
    //   - Five bytes, FF 19 id and a CRC over three: One. Older firmware.
    //   - Nine bytes, FF 19 id, four more, and a CRC over seven: One.
    //   - Anything past the reply: Several. Any other first bytes, or a CRC
    //     that does not match: Garbled.
    //   - Except: a nine-byte reply with a few bytes after it, to a query
    //     for one id, is Garbled. That is too little for a second reply.
    //     T3000 calls it an error on the bus (common.cpp:7406-7407), and
    //     stops scanning the port with the code it uses for MS/TP
    //     (TStatScanner.cpp:1363-1371); here the id is asked again.
    //
    // Two rules T3000 does not have:
    //   - An id outside the range asked about is Garbled, since no device
    //     with that id should have answered.
    //   - More than 13 bytes is Several. T3000 reads 13 and looks no further.
    RangeReply decode_range_reply(const uint8_t* reply, size_t length, const ScanFrame& query);

    // Registers 0-9 of one device, as T3000 reads them
    // (TStatScanner.cpp:1458-1505).
    struct DeviceIdentity
    {
        // Registers 0-3, one byte of the serial each, low first.
        uint32_t serial = 0;

        // In tenths, as the network scan gives it: 538 is 53.8. Register 4
        // alone when it is 240-249, an old Tstat's version; registers 5 and 4
        // together otherwise.
        int firmware = 0;

        uint8_t modbus_id = 0;   // register 6
        uint8_t product   = 0;   // register 7, a ProductClassId
        uint8_t hardware  = 0;   // register 8
    };

    // Reads the reply to identity_read(id): id 03 14, twenty bytes of
    // registers, the CRC. False, with the reason, for anything else,
    // including a Modbus exception.
    bool decode_identity_reply(const uint8_t* reply, size_t length, uint8_t id, DeviceIdentity& out,
                               std::string& error);

    // True when the bytes hold BACnet MS/TP frames: a 55 FF preamble, and
    // another eight bytes on (common.cpp:7334-7341).
    bool has_mstp_preambles(const uint8_t* bytes, size_t length);
}
