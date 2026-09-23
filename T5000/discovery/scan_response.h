#pragma once

// The UDP discovery exchange.
//
// A broadcast asks "who is out there"; every Temco device on the subnet
// answers with one of these. It is the first thing that happens in a scan and
// therefore the first thing that can go wrong, so it gets the same treatment
// as the point structs: the layout written down, parsed explicitly, and
// tested.
//
// PARSED BY OFFSET, NOT BY CASTING A STRUCT. global_define.h:2330 declares a
// union `Str_UPD_SCAN` for this, and it is tempting to memcpy a packet over
// it. Do not. T3000 does not do that either - AddNCToList
// (TStatScanner.cpp:2173-2250) walks the buffer a byte at a time and assigns
// to named fields, so the WIRE layout is defined by the order of those
// assignments, not by the struct's memory layout. Those two are not the same
// thing: the struct has a 4-byte parent_serial_number at wire offset 30,
// which the compiler would move to 32 to align it. A cast would read every
// field after that from the wrong place.
//
// Every single-byte field on the wire is followed by a reserve byte, so the
// early fields sit at even offsets with a stride of 2. Multi-byte fields are
// little-endian and NOT reserve-padded.

#include <stdint.h>
#include <string>

namespace t5000::discovery
{
    // What a scanner sends. Five bytes: the query code and a zero terminator.
    // TStatScanner.cpp:1975-1979.
    constexpr uint8_t kQueryMessage = 100;    // UPD_BROADCAST_QRY_MSG
    constexpr int     kQueryLength  = 5;

    // What a device answers with. Note this is 101, the query code plus one -
    // NOT 0x2f, which is RESPONSE_TOTAL_SUB_INFO, a different message that
    // shares the same socket. The receive loop dispatches on this at
    // TStatScanner.cpp:2039.
    constexpr uint8_t kResponseMessage = 101;  // RESPONSE_MSG

    // Where the broadcast goes, and where replies are listened for. These are
    // two different ports and conflating them is an easy mistake:
    // UDP_BROADCAST_PORT (global_define.h:244) is the destination; 57629 is
    // the local port the scanner binds (TStatScanner.cpp:1934).
    constexpr uint16_t kBroadcastPort = 1234;
    constexpr uint16_t kLocalBindPort = 57629;

    // The shortest packet that can be parsed in full. subnet_protocol is the
    // last field T3000 reads; the struct declares four more after it
    // (command_version, subnet_port, subnet_baudrate, minitype) that
    // AddNCToList never parses, so nothing may depend on them.
    constexpr int kMinimumResponseLength = 64;

    struct ScanResponse
    {
        uint32_t serial_number = 0;

        uint8_t  product_id = 0;      // a ProductClassId; see device/product.h
        uint8_t  modbus_id  = 0;

        uint8_t  ip[4]      = { 0, 0, 0, 0 };
        uint16_t modbus_port = 0;
        uint16_t bacnet_port = 0;

        uint16_t software_version = 0;   // the >= 525 PTP gate reads this
        uint16_t hardware_version = 0;

        uint32_t parent_serial_number = 0;
        uint8_t  station_number = 0;
        std::string panel_name;          // 20 bytes on the wire, trimmed here

        uint32_t object_instance = 0;    // assembled from four scattered bytes

        uint8_t  hardware_info   = 0;    // bit0 zigbee, bit1 wifi
        uint8_t  subnet_protocol = 0;

        // Non-zero means the device is sitting in its bootloader and is not
        // running application code. It will answer a broadcast and nothing
        // else.
        //
        // T3000 returns 0 from AddNCToList when it sees this
        // (TStatScanner.cpp:2256-2261), so such a device silently does not
        // appear in the scan at all. A controller that is present, powered,
        // and answering, but invisible to the tool looking for it, is a bad
        // way to spend an afternoon. T5000 reports it as found-but-in-
        // bootloader instead.
        bool in_bootloader = false;

        // True when the device reported a parent whose four serial bytes were
        // all identical and non-zero. T3000 zeroes that case
        // (TStatScanner.cpp:2222-2231) as a known firmware bug, so the parent
        // is not trustworthy - recorded rather than silently dropped.
        bool parent_serial_was_suspect = false;

        std::string ip_text() const;
    };

    // Fills the five-byte query. Returns how many bytes were written, or 0 if
    // the buffer is too small.
    int build_query(uint8_t* out, int capacity);

    // Parses one datagram. Returns false - leaving `out` untouched - when the
    // buffer is short or is not a discovery response, which includes the
    // other message types that arrive on the same socket.
    bool parse_response(const uint8_t* data, int length, ScanResponse& out,
                        std::string& why_not);
}
