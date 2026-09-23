// Tests for the UDP discovery exchange.
//
// Built around a synthetic packet rather than a capture, because the layout
// was derived by reading AddNCToList's assignment order rather than from a
// specification. These tests are what makes that derivation checkable: each
// field is placed at the offset the parser expects and read back, so a
// mistake shows up as a specific field rather than as "scanning is broken".

#include "scan_response.h"
#include "../testing/check.h"

#include <string.h>

namespace
{
    using namespace t5000::discovery;
    using namespace t5000::testing;

    // A well-formed response with every field set to something distinctive,
    // so a misread lands on a recognisable wrong value instead of a plausible
    // one. Offsets here are written out literally rather than shared with the
    // parser's constants - a test that imports the thing it is checking would
    // agree with the parser even when both are wrong.
    struct Packet
    {
        uint8_t b[128] = {};

        Packet()
        {
            b[0] = 101;          // RESPONSE_MSG
            b[2] = 64;           // length

            b[4]  = 0x78;        // serial, little-endian across stride 2
            b[6]  = 0x56;
            b[8]  = 0x34;
            b[10] = 0x12;        // -> 0x12345678

            b[12] = 88;          // product_id: PM_ESP32_T3_SERIES
            b[14] = 17;          // modbus_id

            b[16] = 192;         // ip
            b[18] = 168;
            b[20] = 1;
            b[22] = 50;

            b[24] = 0x02; b[25] = 0x00;   // modbus_port 2
            b[26] = 0x1A; b[27] = 0x02;   // sw_version 538
            b[28] = 0x06; b[29] = 0x00;   // hw_version 6

            b[30] = 0x21; b[31] = 0x43;   // parent serial 0x87654321
            b[32] = 0x65; b[33] = 0x87;

            b[34] = 0x02;        // object_instance_2
            b[35] = 0x01;        // object_instance_1
            b[36] = 9;           // station_number

            memcpy(b + 37, "Rooftop AHU 3       ", 20);   // padded with spaces

            b[57] = 0x04;        // object_instance_4
            b[58] = 0x03;        // object_instance_3
            b[59] = 0;           // isp_mode: running application code
            b[60] = 0xC0; b[61] = 0xBA;   // bacnet_port 47808 (0xBAC0)
            b[62] = 0x02;        // hardware_info: wifi
            b[63] = 12;          // subnet_protocol: PROTOCOL_BIP_TO_MSTP_TO_MODBUS
        }
    };

    void test_query_is_five_bytes()
    {
        section("the discovery query is the five bytes T3000 sends");

        uint8_t buf[8];
        memset(buf, 0xEE, sizeof(buf));

        check_eq(build_query(buf, sizeof(buf)), 5, "five bytes written");
        check_eq((int)buf[0], 100, "the query code");
        check_eq((int)buf[1], 0, "END_FLAG byte 0");
        check_eq((int)buf[2], 0, "byte 1");
        check_eq((int)buf[3], 0, "byte 2");
        check_eq((int)buf[4], 0, "byte 3");
        check_eq((int)buf[5], 0xEE, "and nothing past the fifth byte is touched");

        check_eq(build_query(buf, 4), 0, "a buffer too small writes nothing");
        check_eq(build_query(nullptr, 64), 0, "a null buffer writes nothing");
    }

    void test_ports_are_not_the_same_port()
    {
        section("the broadcast port and the bind port are different");

        // Easy to conflate, and an earlier reading of this code did exactly
        // that. 1234 is where the query goes; 57629 is where replies are
        // listened for.
        check_eq((int)kBroadcastPort, 1234, "UDP_BROADCAST_PORT");
        check_eq((int)kLocalBindPort, 57629, "the local bind port");
        check(kBroadcastPort != kLocalBindPort, "and they are not the same");

        // Likewise the response code is the query code plus one, not 0x2f -
        // 0x2f is RESPONSE_TOTAL_SUB_INFO, a different message on the same
        // socket.
        check_eq((int)kResponseMessage, (int)kQueryMessage + 1, "response is query+1");
        check(kResponseMessage != 0x2f, "and is not RESPONSE_TOTAL_SUB_INFO");
    }

    void test_every_field_round_trips()
    {
        section("every field is read from the offset it was written to");

        Packet p;
        ScanResponse r;
        std::string why;

        check(parse_response(p.b, 64, r, why), "parses");
        check(why.empty(), "with no complaint");

        check_eq((int)r.serial_number, 0x12345678, "serial, little-endian at stride 2");
        check_eq((int)r.product_id, 88, "product_id");
        check_eq((int)r.modbus_id, 17, "modbus_id");
        check(r.ip_text() == "192.168.1.50", "ip");
        check_eq((int)r.modbus_port, 2, "modbus_port");
        check_eq((int)r.software_version, 538, "software_version");
        check_eq((int)r.hardware_version, 6, "hardware_version");
        check_eq((int)r.bacnet_port, 47808, "bacnet_port");
        check_eq((int)r.parent_serial_number, 0x87654321, "parent_serial_number");
        check_eq((int)r.station_number, 9, "station_number");
        check_eq((int)r.object_instance, 0x04030201, "object_instance, reassembled");
        check_eq((int)r.hardware_info, 2, "hardware_info");
        // Set to 12 rather than 0 deliberately: with 0 on the wire and 0 as
        // the default initialiser, this assertion would pass even if the
        // parser never read the field at all.
        check_eq((int)r.subnet_protocol, 12, "subnet_protocol, the last field parsed");
        check(!r.in_bootloader, "not in bootloader");
        check(!r.parent_serial_was_suspect, "parent serial looks real");
    }

    void test_panel_name_is_trimmed_not_truncated()
    {
        section("the panel name loses its padding but not its content");

        Packet p;
        ScanResponse r;
        std::string why;
        check(parse_response(p.b, 64, r, why), "parses");
        check(r.panel_name == "Rooftop AHU 3", "trailing spaces removed");

        // A name filling all 20 bytes has no terminator, and must not run on
        // into object_instance_4 behind it.
        Packet full;
        memcpy(full.b + 37, "ABCDEFGHIJKLMNOPQRST", 20);
        check(parse_response(full.b, 64, r, why), "parses");
        check(r.panel_name == "ABCDEFGHIJKLMNOPQRST", "exactly 20 characters");
        check_eq((int)r.panel_name.size(), 20, "and stops there");
    }

    void test_bootloader_devices_are_reported_not_dropped()
    {
        section("a device in its bootloader is reported, not hidden");

        // T3000 returns 0 from AddNCToList for these, so a device that is
        // present, powered and answering does not appear in the scan at all.
        Packet p;
        p.b[59] = 1;   // isp_mode non-zero

        ScanResponse r;
        std::string why;

        check(parse_response(p.b, 64, r, why), "still parses");
        check(r.in_bootloader, "and is flagged as in bootloader");
        check_eq((int)r.serial_number, 0x12345678, "with its identity intact");
    }

    void test_the_airlab_parent_bug_is_flagged()
    {
        section("an impossible parent serial is zeroed AND flagged");

        Packet p;
        p.b[30] = p.b[31] = p.b[32] = p.b[33] = 0x55;   // all four identical

        ScanResponse r;
        std::string why;
        check(parse_response(p.b, 64, r, why), "parses");
        check_eq((int)r.parent_serial_number, 0, "the bogus parent is cleared");
        check(r.parent_serial_was_suspect, "and the fact is recorded");

        // All-zero is the ordinary "no parent" case and must NOT be flagged
        // as suspect - the guard in T3000 tests parent[0] != 0 for exactly
        // this reason.
        Packet none;
        none.b[30] = none.b[31] = none.b[32] = none.b[33] = 0;
        check(parse_response(none.b, 64, r, why), "parses");
        check_eq((int)r.parent_serial_number, 0, "no parent");
        check(!r.parent_serial_was_suspect, "which is not suspicious");
    }

    void test_other_traffic_is_declined_quietly()
    {
        section("other messages on the socket are declined with a reason");

        Packet p;
        ScanResponse r;
        std::string why;

        // 0x2f is a real message type that arrives on this same socket.
        p.b[0] = 0x2f;
        check(!parse_response(p.b, 64, r, why), "0x2f is not a discovery response");
        check(!why.empty(), "and says so");

        p.b[0] = 100;   // our own query, seen because the socket is broadcast
        check(!parse_response(p.b, 64, r, why), "the query itself is not a response");
    }

    void test_short_packets_are_refused()
    {
        section("a truncated response is refused, not read past");

        Packet p;
        ScanResponse r;
        std::string why;

        check(!parse_response(p.b, 63, r, why), "one byte short is refused");
        check(!why.empty(), "with a reason naming the length");
        check(parse_response(p.b, 64, r, why), "exactly enough is accepted");

        check(!parse_response(p.b, 0, r, why), "zero length");
        check(!parse_response(nullptr, 64, r, why), "null data");

        // A refusal must leave the caller's object alone rather than
        // half-filling it - a partly-written device record is worse than none.
        ScanResponse untouched;
        untouched.serial_number = 4242;
        check(!parse_response(p.b, 10, untouched, why), "refused");
        check_eq((int)untouched.serial_number, 4242, "and left untouched");
    }
}

int run_scan_response_tests()
{
    test_query_is_five_bytes();
    test_ports_are_not_the_same_port();
    test_every_field_round_trips();
    test_panel_name_is_trimmed_not_truncated();
    test_bootloader_devices_are_reported_not_dropped();
    test_the_airlab_parent_bug_is_flagged();
    test_other_traffic_is_declined_quietly();
    test_short_packets_are_refused();
    return 0;
}
