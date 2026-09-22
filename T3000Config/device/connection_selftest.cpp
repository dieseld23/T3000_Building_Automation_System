// Tests for connection settings.
//
// Validation is the point of this module, so most of these assert that a bad
// setting is REJECTED. A settings form that accepts nonsense and fails later,
// against live equipment, is worse than one that refuses up front.

#include "connection.h"
#include "../testing/check.h"

#include <string.h>

namespace
{
    using namespace t3000::device;
    using namespace t3000::testing;

    Connection valid_ip()
    {
        Connection c;
        c.transport = Transport::BacnetIp;
        c.host = "192.168.1.50";
        c.udp_port = 47808;
        c.device_instance = 1;
        return c;
    }

    Connection valid_mstp()
    {
        Connection c;
        c.transport = Transport::BacnetMstp;
        c.com_port = 3;
        c.baud = 38400;
        c.mstp_max_master = 127;
        return c;
    }

    bool has_error_on(const std::vector<ValidationError>& errs, const char* field)
    {
        for (const auto& e : errs)
            if (e.field == field) return true;
        return false;
    }

    void test_defaults_are_usable()
    {
        section("a default BACnet/IP config only complains about the host");

        Connection c;   // untouched defaults
        auto errs = validate(c);

        // Defaults should not be self-inconsistent. The only thing a fresh
        // install cannot know is where the controller is.
        check(has_error_on(errs, "host"), "an empty host is rejected");
        check(!has_error_on(errs, "udpPort"), "the default UDP port is accepted");
        check(!has_error_on(errs, "deviceInstance"), "the default device instance is accepted");
    }

    void test_host_rejects_what_people_actually_paste()
    {
        section("host rejects URLs, ports and paths");

        Connection c = valid_ip();
        check(validate(c).empty(), "a bare IP is accepted");

        c.host = "http://192.168.1.50";
        check(has_error_on(validate(c), "host"), "a URL is rejected");

        c.host = "192.168.1.50:47808";
        check(has_error_on(validate(c), "host"), "host:port is rejected");

        c.host = "192.168.1.50/bacnet";
        check(has_error_on(validate(c), "host"), "a path is rejected");

        c.host = "192.168.1.50 ";
        check(has_error_on(validate(c), "host"), "trailing whitespace is rejected");
    }

    void test_serial_ignores_host_and_checks_port()
    {
        section("serial transports validate the COM port, not the host");

        Connection c = valid_mstp();
        check(validate(c).empty(), "a valid MSTP config passes with no host set");

        c.com_port = 0;
        check(has_error_on(validate(c), "comPort"), "COM0 is rejected");

        c.com_port = 300;
        check(has_error_on(validate(c), "comPort"), "COM300 is rejected");
    }

    void test_baud_must_be_one_the_hardware_offers()
    {
        section("baud is restricted to the rates the controllers offer");

        Connection c = valid_mstp();

        for (int rate : supported_baud_rates())
        {
            c.baud = rate;
            check(!has_error_on(validate(c), "baud"), "a supported baud rate is accepted");
        }

        // 14400 is a real RS-485 rate and a plausible typo, but not one of the
        // six this product offers - so it is exactly the kind of value that
        // should fail here rather than at the controller.
        c.baud = 14400;
        check(has_error_on(validate(c), "baud"), "an unsupported baud rate is rejected");

        c.baud = 0;
        check(has_error_on(validate(c), "baud"), "zero baud is rejected");
    }

    void test_unusual_bacnet_port_is_flagged()
    {
        section("a non-standard BACnet/IP port is flagged, not silently accepted");

        Connection c = valid_ip();
        c.udp_port = 47809;
        check(has_error_on(validate(c), "udpPort"), "47809 is flagged");

        c.udp_port = 47808;
        check(!has_error_on(validate(c), "udpPort"), "47808 is not");
    }

    void test_modbus_slave_id_range()
    {
        section("Modbus slave id is 1..247");

        Connection c;
        c.transport = Transport::ModbusTcp;
        c.host = "10.0.0.5";
        c.modbus_slave_id = 1;
        check(!has_error_on(validate(c), "modbusSlaveId"), "1 is accepted");

        c.modbus_slave_id = 247;
        check(!has_error_on(validate(c), "modbusSlaveId"), "247 is accepted");

        c.modbus_slave_id = 248;
        check(has_error_on(validate(c), "modbusSlaveId"), "248 is rejected");

        c.modbus_slave_id = 0;
        check(has_error_on(validate(c), "modbusSlaveId"), "0 is rejected");
    }

    void test_validate_reports_every_problem()
    {
        section("validation reports every problem, not just the first");

        Connection c;
        c.transport = Transport::BacnetMstp;
        c.com_port = 0;        // bad
        c.baud = 12345;        // bad
        c.mstp_max_master = 0; // bad

        // One error per submit means five submits. All three should come back
        // together.
        check(validate(c).size() >= 3, "three independent problems are all reported");
    }

    void test_json_round_trip()
    {
        section("settings survive a JSON round trip");

        Connection original = valid_mstp();
        original.host = "controller.local";
        original.device_instance = 77;
        original.modbus_slave_id = 12;

        Connection restored;
        std::string error;
        check(from_json(to_json(original), restored, error), "parsed back");

        check(restored.transport == original.transport, "transport");
        check(restored.host == original.host, "host");
        check_eq(restored.com_port, original.com_port, "comPort");
        check_eq(restored.baud, original.baud, "baud");
        check_eq(restored.device_instance, original.device_instance, "deviceInstance");
        check_eq(restored.mstp_max_master, original.mstp_max_master, "mstpMaxMaster");
        check_eq(restored.modbus_slave_id, original.modbus_slave_id, "modbusSlaveId");
    }

    void test_json_is_tolerant_but_not_silent()
    {
        section("loading is tolerant of extra keys, loud about a bad transport");

        Connection c;
        std::string error;

        // A config written by a newer build must still load.
        check(from_json("{\"transport\":\"bacnet-ip\",\"host\":\"1.2.3.4\","
                        "\"somethingNew\":42}", c, error),
              "unknown keys are ignored");
        check(c.host == "1.2.3.4", "known keys still read correctly");

        // A missing key keeps its default rather than becoming zero.
        Connection d;
        check(from_json("{\"host\":\"1.2.3.4\"}", d, error), "a partial object loads");
        check_eq(d.udp_port, 47808, "an absent key keeps its default");

        // But a transport that does not exist is a real mistake, not a
        // forward-compatible extra.
        check(!from_json("{\"transport\":\"carrier-pigeon\"}", c, error),
              "an unknown transport is rejected");
        check(!error.empty(), "and says what was expected");
    }
}

int run_connection_tests()
{
    test_defaults_are_usable();
    test_host_rejects_what_people_actually_paste();
    test_serial_ignores_host_and_checks_port();
    test_baud_must_be_one_the_hardware_offers();
    test_unusual_bacnet_port_is_flagged();
    test_modbus_slave_id_range();
    test_validate_reports_every_problem();
    test_json_round_trip();
    test_json_is_tolerant_but_not_silent();
    return 0;
}
