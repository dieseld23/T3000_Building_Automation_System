// Tests for the scan logic.
//
// The transport is faked, which is the whole point of it being an interface:
// deciding what a response means needs no subnet, and that is where the
// mistakes are. What a real socket does with a real controller is not
// testable here and is not pretended to be.

#include "scanner.h"
#include "../testing/check.h"

#include <string.h>

namespace
{
    using namespace t5000::discovery;
    using namespace t5000::device;
    using namespace t5000::testing;

    // A fake subnet: hands out queued datagrams, then times out.
    class FakeTransport : public ScanTransport
    {
    public:
        std::vector<std::vector<uint8_t>> queued;
        bool fail_broadcast = false;
        int  fail_receive_after = -1;   // -1 never
        int  broadcasts = 0;
        int  receives = 0;

        bool broadcast_query(std::string& error) override
        {
            broadcasts++;
            if (fail_broadcast) { error = "no route to host"; return false; }
            return true;
        }

        int receive(uint8_t* buffer, int capacity, int, std::string& error) override
        {
            if (fail_receive_after >= 0 && receives >= fail_receive_after)
            {
                error = "socket died";
                return -1;
            }
            receives++;

            if (queued.empty()) return 0;   // nothing left: time out

            const auto d = queued.front();
            queued.erase(queued.begin());
            const int n = (int)d.size() < capacity ? (int)d.size() : capacity;
            memcpy(buffer, d.data(), n);
            return n;
        }
    };

    std::vector<uint8_t> a_response(uint32_t serial, uint8_t product = 88,
                                    uint8_t modbus_id = 1, uint8_t last_ip = 50)
    {
        std::vector<uint8_t> b(64, 0);
        b[0] = 101;
        b[2] = 64;
        b[4]  = (uint8_t)(serial & 0xFF);
        b[6]  = (uint8_t)((serial >> 8) & 0xFF);
        b[8]  = (uint8_t)((serial >> 16) & 0xFF);
        b[10] = (uint8_t)((serial >> 24) & 0xFF);
        b[12] = product;
        b[14] = modbus_id;
        b[16] = 192; b[18] = 168; b[20] = 1; b[22] = last_ip;
        b[26] = 0x1A; b[27] = 0x02;   // sw_version 538
        b[60] = 0xC0; b[61] = 0xBA;   // bacnet port 47808
        return b;
    }

    ScanSettings quick()
    {
        ScanSettings s;
        s.total_timeout_ms = 30;
        s.slice_timeout_ms = 10;
        return s;
    }

    void test_a_quiet_subnet_is_not_an_error()
    {
        section("finding nothing is a valid answer, not a failure");

        FakeTransport t;
        const auto result = scan(t, quick());

        check(result.ok(), "no error");
        check(result.devices.empty(), "no devices");
        check_eq(t.broadcasts, 1, "the query was sent once");
        check_eq(result.stats.datagrams_received, 0, "nothing came back");
    }

    void test_a_failed_broadcast_stops_the_scan()
    {
        section("a scan that cannot send says so");

        FakeTransport t;
        t.fail_broadcast = true;
        const auto result = scan(t, quick());

        check(!result.ok(), "reports an error");
        check(result.error.find("no route") != std::string::npos,
              "and passes the reason through rather than inventing one");
        check(result.devices.empty(), "with no devices");
    }

    void test_responses_become_devices()
    {
        section("a response becomes a device record");

        FakeTransport t;
        t.queued.push_back(a_response(500123, 88, 7, 50));
        const auto result = scan(t, quick());

        check(result.ok(), "no error");
        check_eq((int)result.devices.size(), 1, "one device");

        const auto& d = result.devices[0];
        check_eq((long)d.serial_number, 500123, "serial");
        check(d.product == ProductClassId::Esp32T3Series, "product id mapped");
        check_eq(d.firmware, 538, "firmware, which the PTP gate needs");
        check(d.provenance == Provenance::BacnetBroadcast, "provenance");
        check(d.reached, "it answered, so it is there");
        check(d.connection.host == "192.168.1.50", "reachable at the address it gave");
        check_eq(d.connection.udp_port, 47808, "on the port it named");
        check_eq(d.connection.modbus_slave_id, 7, "modbus id carried over");
        check(d.repairs.empty(), "a healthy device needs no repairs");
    }

    void test_a_missing_serial_proposes_a_repair_and_nothing_else()
    {
        section("a device with no serial gets a proposal, not a write");

        FakeTransport t;
        t.queued.push_back(a_response(0));
        t.queued.push_back(a_response(0xFFFFFFFF, 88, 2, 51));
        const auto result = scan(t, quick());

        check_eq((int)result.devices.size(), 2, "both devices are listed");
        check_eq(result.stats.without_serial, 2, "both are counted as unidentified");

        for (const auto& d : result.devices)
        {
            check_eq((int)d.repairs.size(), 1, "one repair proposed");
            check(d.repairs[0].kind == RepairKind::AssignSerialNumber, "the right kind");
            check(!d.repairs[0].approved, "NOT approved - nothing was written");
            check(!d.repairs[0].reversible, "and it is flagged as irreversible");
            check(!d.repairs[0].consequence.empty(), "the consequence is spelled out");
        }

        // The randomness matters enough to say out loud: two devices repaired
        // in the same second can be handed the same "unique" number.
        check(result.devices[0].repairs[0].consequence.find("RANDOM") != std::string::npos,
              "and warns that the number is random");
    }

    void test_a_reported_id_of_zero_stays_zero()
    {
        section("a device that reports no Modbus id is not recorded as id 1");

        // Connection::modbus_slave_id defaults to 1, and to_record only
        // assigns it when the wire value is non-zero. Reading duplicates off
        // that field therefore saw every device that reported NO id as
        // sitting on id 1 - and accused them all of conflicting with whatever
        // is genuinely there.
        //
        // The earlier version of the duplicate tests set the field by hand and
        // so never went through this path. This one does.
        ScanResponse silent;
        silent.serial_number = 900001;
        silent.modbus_id     = 0;

        const DeviceRecord d = to_record(silent);
        check_eq(d.modbus_id_reported, 0, "reported id stays 0, meaning 'did not say'");

        ScanResponse speaks;
        speaks.serial_number = 900002;
        speaks.modbus_id     = 7;

        const DeviceRecord e = to_record(speaks);
        check_eq(e.modbus_id_reported, 7, "and a real one is carried through");

        // Three silent devices must not be a three-way conflict on id 1.
        Registry reg;
        reg.add_or_merge(to_record(silent));
        ScanResponse silent2 = silent; silent2.serial_number = 900003;
        ScanResponse silent3 = silent; silent3.serial_number = 900004;
        reg.add_or_merge(to_record(silent2));
        reg.add_or_merge(to_record(silent3));

        check_eq(reg.refresh_duplicate_modbus_ids(), 0,
                 "three devices that reported nothing are not in conflict");
    }

    void test_the_bacnet_instance_is_kept()
    {
        section("the BACnet device instance from the response reaches the record");

        // It was parsed, then dropped: every scanned device carried instance
        // 0, which nothing noticed because nothing read it yet.
        ScanResponse r;
        r.serial_number   = 900010;
        r.object_instance = 123456;
        check_eq(to_record(r).connection.device_instance, 123456, "instance carried through");

        // And a rescan that does not report one does not erase it.
        Registry reg;
        reg.add_or_merge(to_record(r));
        ScanResponse again = r;
        again.object_instance = 0;
        reg.add_or_merge(to_record(again));
        check_eq(reg.devices()[0].connection.device_instance, 123456,
                 "a later response without one keeps the earlier instance");
    }

    void test_a_device_behind_a_controller_is_marked()
    {
        section("a device a controller answered for is marked as behind it");

        ScanResponse child;
        child.serial_number        = 900020;
        child.parent_serial_number = 500001;
        check_eq((long)to_record(child).parent_serial, 500001, "the parent's serial is kept");

        // Moved onto the network itself: a complete observation saying
        // "no parent" must clear it, or the device stays refused forever.
        Registry reg;
        reg.add_or_merge(to_record(child));
        ScanResponse moved = child;
        moved.parent_serial_number = 0;
        reg.add_or_merge(to_record(moved));
        check_eq((long)reg.devices()[0].parent_serial, 0,
                 "a later complete observation with no parent clears it");

        // A partial observation knows nothing about parents and must not.
        reg.add_or_merge(to_record(child));
        DeviceRecord partial;
        partial.serial_number        = 900020;
        partial.observation_complete = false;
        reg.add_or_merge(partial);
        check_eq((long)reg.devices()[0].parent_serial, 500001,
                 "a partial observation leaves it alone");
    }

    void test_bootloader_devices_are_listed_and_counted()
    {
        section("a device in its bootloader still appears");

        FakeTransport t;
        auto boot = a_response(3001);
        boot[59] = 1;   // isp_mode
        t.queued.push_back(boot);
        const auto result = scan(t, quick());

        check_eq((int)result.devices.size(), 1, "it is listed");
        check_eq(result.stats.in_bootloader, 1, "and counted");
        check(result.devices[0].reached, "it answered, so it was reached");
    }

    void test_foreign_traffic_and_malformed_responses_are_told_apart()
    {
        section("noise is ignored; a broken response is reported");

        FakeTransport t;
        t.queued.push_back(std::vector<uint8_t>(64, 0));        // type 0: noise
        { auto q = a_response(1); q[0] = 100; t.queued.push_back(q); }  // our own query
        { auto q = a_response(1); q[0] = 0x2f; t.queued.push_back(q); } // other message
        { auto q = a_response(4001); q.resize(20); t.queued.push_back(q); } // ours, truncated
        t.queued.push_back(a_response(4002));                   // a good one

        const auto result = scan(t, quick());

        check_eq(result.stats.datagrams_received, 5, "five datagrams seen");
        check_eq((int)result.devices.size(), 1, "one real device");
        check_eq(result.stats.ignored, 3, "three were not for us");

        // The distinction matters: a device answering with something
        // unreadable is a firmware problem worth raising, whereas other
        // traffic on a shared socket is not.
        check_eq(result.stats.malformed, 1, "one was ours but unreadable");
    }

    void test_a_dying_socket_keeps_what_was_found()
    {
        section("a socket failure mid-scan keeps the devices already found");

        FakeTransport t;
        t.queued.push_back(a_response(5001));
        t.queued.push_back(a_response(5002));
        t.fail_receive_after = 2;   // dies on the third receive

        const auto result = scan(t, quick());

        check(!result.ok(), "the failure is reported");
        check_eq((int)result.devices.size(), 2, "and the two real devices survive it");
    }

    void test_the_device_cap_is_reported_not_silent()
    {
        section("hitting the device cap says the list is incomplete");

        FakeTransport t;
        for (int i = 0; i < 6; i++)
            t.queued.push_back(a_response(6000 + i, 88, (uint8_t)(i + 1), (uint8_t)(50 + i)));

        ScanSettings s = quick();
        s.max_devices = 3;
        const auto result = scan(t, s);

        // Silently truncating would read as "these are all the devices",
        // which is the one thing a scan must never say wrongly.
        check(!result.ok(), "reports a problem");
        check(result.error.find("incomplete") != std::string::npos,
              "and says the list is incomplete");
        check_eq((int)result.devices.size(), 3, "with the cap respected");
    }

}

int run_scanner_tests()
{
    test_a_quiet_subnet_is_not_an_error();
    test_a_failed_broadcast_stops_the_scan();
    test_responses_become_devices();
    test_a_missing_serial_proposes_a_repair_and_nothing_else();
    test_a_reported_id_of_zero_stays_zero();
    test_the_bacnet_instance_is_kept();
    test_a_device_behind_a_controller_is_marked();
    test_bootloader_devices_are_listed_and_counted();
    test_foreign_traffic_and_malformed_responses_are_told_apart();
    test_a_dying_socket_keeps_what_was_found();
    test_the_device_cap_is_reported_not_silent();
    return 0;
}
