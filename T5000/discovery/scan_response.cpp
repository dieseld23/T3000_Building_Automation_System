#include "scan_response.h"

namespace t5000::discovery
{
    namespace
    {
        // Wire offsets, counted off the assignment order in
        // AddNCToList (TStatScanner.cpp:2180-2250). Single-byte fields are
        // each followed by a reserve byte, hence the stride of 2; multi-byte
        // fields are little-endian and have no reserve.
        enum : int
        {
            kCommand        = 0,
            kLength         = 2,
            kSerialByte0    = 4,     // then 6, 8, 10 - little-endian
            kProductId      = 12,
            kModbusId       = 14,
            kIpByte0        = 16,    // then 18, 20, 22
            kModbusPort     = 24,    // uint16 LE
            kSwVersion      = 26,    // uint16 LE
            kHwVersion      = 28,    // uint16 LE
            kParentSerial   = 30,    // uint32 LE
            kObjInstance2   = 34,
            kObjInstance1   = 35,
            kStationNumber  = 36,
            kPanelName      = 37,    // 20 bytes, no reserve padding
            kPanelNameLen   = 20,
            kObjInstance4   = 57,
            kObjInstance3   = 58,
            kIspMode        = 59,
            kBacnetPort     = 60,    // uint16 LE
            kHardwareInfo   = 62,
            kSubnetProtocol = 63,
        };

        static_assert(kSubnetProtocol < kMinimumResponseLength,
            "the minimum length must cover every field this parser reads");

        uint16_t le16(const uint8_t* p)
        {
            return (uint16_t)((uint16_t)p[1] << 8 | (uint16_t)p[0]);
        }

        uint32_t le32(const uint8_t* p)
        {
            return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 |
                   (uint32_t)p[1] << 8  | (uint32_t)p[0];
        }
    }

    std::string ScanResponse::ip_text() const
    {
        return std::to_string(ip[0]) + '.' + std::to_string(ip[1]) + '.' +
               std::to_string(ip[2]) + '.' + std::to_string(ip[3]);
    }

    int build_query(uint8_t* out, int capacity)
    {
        if (!out || capacity < kQueryLength)
            return 0;

        // TStatScanner.cpp:1975-1979: the code byte, then a four-byte zero
        // END_FLAG. Five bytes total.
        out[0] = kQueryMessage;
        out[1] = out[2] = out[3] = out[4] = 0;
        return kQueryLength;
    }

    bool parse_response(const uint8_t* data, int length, ScanResponse& out,
                        std::string& why_not)
    {
        if (!data || length <= 0)
        {
            why_not = "empty datagram";
            return false;
        }

        // Other message types share this socket, so a non-match is ordinary
        // traffic rather than an error worth alarming anyone about.
        if (data[0] != kResponseMessage)
        {
            why_not = "not a discovery response (message type " +
                      std::to_string((int)data[0]) + ", expected " +
                      std::to_string((int)kResponseMessage) + ")";
            return false;
        }

        if (length < kMinimumResponseLength)
        {
            why_not = "discovery response truncated: " + std::to_string(length) +
                      " bytes, need at least " + std::to_string(kMinimumResponseLength);
            return false;
        }

        ScanResponse r;

        // Serial is four single bytes at stride 2, assembled little-endian.
        r.serial_number = (uint32_t)data[kSerialByte0] |
                          (uint32_t)data[kSerialByte0 + 2] << 8 |
                          (uint32_t)data[kSerialByte0 + 4] << 16 |
                          (uint32_t)data[kSerialByte0 + 6] << 24;

        r.product_id = data[kProductId];
        r.modbus_id  = data[kModbusId];

        for (int i = 0; i < 4; i++)
            r.ip[i] = data[kIpByte0 + i * 2];

        r.modbus_port      = le16(data + kModbusPort);
        r.software_version = le16(data + kSwVersion);
        r.hardware_version = le16(data + kHwVersion);
        r.bacnet_port      = le16(data + kBacnetPort);

        // A device whose reported parent serial is four identical non-zero
        // bytes is exhibiting a known firmware bug, so the value means
        // nothing. T3000 silently zeroes it; we zero it too but say we did,
        // because "this device reports a parent that cannot be real" is
        // useful when a tree comes out the wrong shape.
        const uint8_t* parent = data + kParentSerial;
        if (parent[0] != 0 &&
            parent[0] == parent[1] && parent[0] == parent[2] && parent[0] == parent[3])
        {
            r.parent_serial_number      = 0;
            r.parent_serial_was_suspect = true;
        }
        else
        {
            r.parent_serial_number = le32(parent);
        }

        r.station_number = data[kStationNumber];

        // The four instance bytes are scattered and out of order on the wire,
        // which is why they are assembled here rather than read as a word.
        r.object_instance = (uint32_t)data[kObjInstance1] |
                            (uint32_t)data[kObjInstance2] << 8 |
                            (uint32_t)data[kObjInstance3] << 16 |
                            (uint32_t)data[kObjInstance4] << 24;

        // Fixed 20 bytes, not necessarily terminated. Stop at the first NUL
        // and drop trailing blanks, so a name padded with spaces does not
        // come out with them attached.
        {
            const char* name = (const char*)(data + kPanelName);
            int n = 0;
            while (n < kPanelNameLen && name[n] != '\0') n++;
            while (n > 0 && (name[n - 1] == ' ' || name[n - 1] == '\t')) n--;
            r.panel_name.assign(name, name + n);
        }

        r.in_bootloader   = data[kIspMode] != 0;
        r.hardware_info   = data[kHardwareInfo];
        r.subnet_protocol = data[kSubnetProtocol];

        out = r;
        why_not.clear();
        return true;
    }
}
