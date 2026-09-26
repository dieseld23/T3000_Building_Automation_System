// Tests for the serial scan.
//
// The line is scripted (testing/fake_serial_line.h): devices on it read the
// bytes sent and answer as Temco devices do, and several answering at once
// either follow each other or collide. No port is opened. What a real adapter
// does with a real bus is not testable here and is not pretended to be.
//
// Every test ends by reading back each frame sent, from its bytes: all of
// them must be a range query or a read of registers 0-9.

#include "serial_scan.h"
#include "../testing/check.h"
#include "../testing/fake_serial_line.h"

#include <type_traits>

namespace
{
    using namespace t5000::discovery;
    using namespace t5000::device;
    using namespace t5000::testing;
    using t5000::serial::ScanFrame;

    // The line can be handed a ScanFrame and nothing else.
    static_assert(std::is_same_v<decltype(&SerialScanTransport::send),
                                 bool (SerialScanTransport::*)(const ScanFrame&, std::string&)>,
                  "a serial scan's line takes scan frames, not bytes");

    SerialScanSettings settings()
    {
        SerialScanSettings s;
        s.com_port = 5;
        s.baud     = 19200;
        return s;
    }

    SerialDevice device(uint8_t id, uint32_t serial)
    {
        SerialDevice d;
        d.id     = id;
        d.serial = serial;
        return d;
    }

    void only_scan_frames(const FakeSerialLine& line)
    {
        check_eq(line.frames_not_scan_frames(), 0, "every frame sent was a range query or a read of registers 0-9");
        check_eq(line.refused_empty, 0, "and none was empty");
    }

    std::vector<int> ids_found(const SerialScanResult& r)
    {
        std::vector<int> ids;
        for (const DeviceRecord& d : r.devices)
            ids.push_back(d.connection.modbus_slave_id);
        return ids;
    }

    void test_the_fake_line_speaks_modbus()
    {
        section("the scripted line's CRC is CRC-16/MODBUS");

        const uint8_t check_string[] = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };
        check_eq(line_crc(check_string, sizeof(check_string)), 0x4B37, "the standard's check value");
        check(is_scan_frame({ 0x01, 0x03, 0x00, 0x00, 0x00, 0x0A, 0xC5, 0xCD }), "a read of id 1 is a scan frame");
        check(!is_scan_frame({ 0x01, 0x06, 0x00, 0x0A, 0x00, 0x05, 0x69, 0xCB }), "a write of register 10 is not");
        check(!is_scan_frame({ 0x01, 0x03, 0x00, 0x00, 0x00, 0x0A, 0xC5, 0xCE }), "nor is a frame with a bad CRC");
    }

    void test_a_quiet_line()
    {
        section("a line with nothing on it: one query, and no devices");

        FakeSerialLine line;
        const SerialScanResult r = scan_serial(line, settings());

        check(r.devices.empty(), "no devices");
        check(r.error.empty(), "and no error: an empty line is not one");
        check(r.line_busy.empty(), "the line was free");
        check_eq((long)line.sent.size(), 1, "one frame was sent");
        if (require(line.sent.size() == 1, "one frame"))
            check(line.sent[0] == SerialBytes({ 0xFF, 0x19, 0xFE, 0x01, 0x60, 0x57 }), "asking ids 1-254");
        check_eq(r.stats.frames_sent, 1, "and counted");
        if (require(line.timeouts.size() == 2, "it listened, then waited for the reply"))
        {
            check_eq(line.timeouts[0], 1500, "listening for the time set");
            check_eq(line.timeouts[1], 500, "and waiting for the reply for the time set");
        }
        only_scan_frames(line);
    }

    void test_one_device_is_found_and_read()
    {
        section("one device is found, and its registers 0-9 read");

        FakeSerialLine line;
        SerialDevice d = device(12, 123456);
        d.product  = 88;
        d.firmware = 538;
        line.devices.push_back(d);

        const SerialScanResult r = scan_serial(line, settings());
        if (!require(r.devices.size() == 1, "one device"))
            return;

        const DeviceRecord& rec = r.devices[0];
        check_eq((long)rec.serial_number, 123456, "its serial");
        check_eq((long)rec.product, 88, "its product");
        check_eq(rec.firmware, 538, "its firmware");
        check(rec.provenance == Provenance::SerialScan, "found by a serial scan");
        check(rec.reached, "it answered");
        check(rec.observation_complete, "and was read in full");
        check(rec.connection.transport == Transport::ModbusRtu, "reached over Modbus RTU");
        check_eq(rec.connection.com_port, 5, "on the port scanned");
        check_eq(rec.connection.baud, 19200, "at the rate scanned");
        check_eq(rec.connection.modbus_slave_id, 12, "at the id that answered");
        check_eq(rec.modbus_id_reported, 12, "which is the id it reports");
        check_streq(rec.address_note.c_str(), "COM5 id 12, 19200 baud", "and says where it is");
        check(rec.repairs.empty(), "with nothing to repair");

        check_eq((long)line.sent.size(), 2, "a query and a read");
        check_eq(line.reads_of(12), 1, "id 12 was read once");
        check(r.error.empty() && r.stats.shared_ids.empty() && r.stats.unreadable_ids.empty(),
              "and nothing went wrong");
        only_scan_frames(line);
    }

    void test_devices_answering_together_are_told_apart()
    {
        section("devices answering at once are told apart by halving the range");

        FakeSerialLine line;
        for (int id : { 1, 12, 13, 100, 254 })
            line.devices.push_back(device((uint8_t)id, 1000 + id));

        const SerialScanResult r = scan_serial(line, settings());
        check(ids_found(r) == std::vector<int>({ 1, 12, 13, 100, 254 }), "all five are found, in id order");
        for (const DeviceRecord& d : r.devices)
            check_eq((long)d.serial_number, 1000 + d.connection.modbus_slave_id, "each with its own serial");
        check_eq(r.stats.garbled, 0, "replies one after another are several, not garbled");
        check(r.stats.shared_ids.empty() && r.stats.unreadable_ids.empty(), "and none is left out");
        only_scan_frames(line);
    }

    void test_colliding_replies_are_told_apart()
    {
        section("devices whose replies collide are told apart too");

        FakeSerialLine line;
        line.collision = FakeSerialLine::Collision::Overlay;
        for (int id : { 1, 12, 13, 100, 254 })
            line.devices.push_back(device((uint8_t)id, 1000 + id));

        const SerialScanResult r = scan_serial(line, settings());
        check(ids_found(r) == std::vector<int>({ 1, 12, 13, 100, 254 }), "all five are found");
        check(r.stats.garbled > 0, "and the collisions are counted as garbled");
        check(r.stats.unreadable_ids.empty(), "with none left unread");
        only_scan_frames(line);
    }

    void test_a_full_range_is_searched_in_the_expected_frames()
    {
        section("sixteen devices on sixteen ids take 31 queries and 16 reads");

        FakeSerialLine line;
        for (int id = 1; id <= 16; id++)
            line.devices.push_back(device((uint8_t)id, 5000 + id));

        SerialScanSettings s = settings();
        s.lowest  = 1;
        s.highest = 16;
        const SerialScanResult r = scan_serial(line, s);

        check_eq((long)r.devices.size(), 16, "all sixteen are found");
        check_eq(line.range_queries(), 31, "each range asked once: 16 + 8 + 4 + 2 + 1");
        for (int id = 1; id <= 16; id++)
            check_eq(line.reads_of((uint8_t)id), 1, "each id read once");
        check_eq(r.stats.frames_sent, 47, "and every frame counted");
        only_scan_frames(line);
    }

    void test_only_the_range_asked_is_scanned()
    {
        section("only the ids asked about are scanned");

        FakeSerialLine line;
        for (int id : { 5, 15, 30 })
            line.devices.push_back(device((uint8_t)id, 7000 + id));

        SerialScanSettings s = settings();
        s.lowest  = 10;
        s.highest = 20;
        const SerialScanResult r = scan_serial(line, s);

        check(ids_found(r) == std::vector<int>({ 15 }), "only id 15");
        if (require(!line.sent.empty(), "something was sent"))
            check(line.sent[0] == SerialBytes({ 0xFF, 0x19, 0x14, 0x0A, 0x6E, 0xF0 }), "starting with 10-20");
        only_scan_frames(line);
    }

    void test_older_firmware_is_found()
    {
        section("a device answering in five bytes is found");

        FakeSerialLine line;
        SerialDevice d = device(40, 222);
        d.short_range_reply = true;
        line.devices.push_back(d);
        line.devices.push_back(device(41, 223));

        const SerialScanResult r = scan_serial(line, settings());
        check(ids_found(r) == std::vector<int>({ 40, 41 }), "both are found, the five-byte one too");
        only_scan_frames(line);
    }

    void test_a_shared_id_is_reported_and_not_changed()
    {
        section("two devices on one id are reported, not moved");

        FakeSerialLine line;
        line.devices.push_back(device(12, 111));
        line.devices.push_back(device(12, 222));
        line.devices.push_back(device(40, 333));

        const SerialScanResult r = scan_serial(line, settings());
        check(ids_found(r) == std::vector<int>({ 40 }), "the device alone on its id is found");
        check(r.stats.shared_ids == std::vector<int>({ 12 }), "id 12 is reported as shared");
        check_eq(line.reads_of(12), 0, "and neither device on it is read");

        // T3000 writes register 10 here, to move one of them. Nothing can.
        only_scan_frames(line);
    }

    void test_a_collision_on_one_id_is_unreadable()
    {
        section("two devices colliding on one id are unreadable, after the set number of tries");

        FakeSerialLine line;
        line.collision = FakeSerialLine::Collision::Overlay;
        line.devices.push_back(device(12, 111));
        line.devices.push_back(device(12, 222));

        SerialScanSettings s = settings();
        s.attempts = 4;
        const SerialScanResult r = scan_serial(line, s);

        check(r.devices.empty(), "neither is listed");
        check(r.stats.unreadable_ids == std::vector<int>({ 12 }), "id 12 is reported as unreadable");
        check(r.stats.shared_ids.empty(), "colliding replies cannot be told from noise, so it is not called shared");
        check_eq(line.queries_for_id(12), 4, "id 12 was asked four times, as set, and no more");
        only_scan_frames(line);
    }

    void test_noise_is_retried_a_set_number_of_times()
    {
        section("a garbled reply is asked again, a set number of times");

        {
            FakeSerialLine line;
            SerialDevice d = device(12, 444);
            d.garble_identity_replies = 1;
            line.devices.push_back(d);

            const SerialScanResult r = scan_serial(line, settings());
            check(ids_found(r) == std::vector<int>({ 12 }), "a read garbled once is found");
            check_eq(line.reads_of(12), 2, "on the second read");
        }
        {
            FakeSerialLine line;
            SerialDevice d = device(12, 444);
            d.garble_range_replies = 1;
            line.devices.push_back(d);

            const SerialScanResult r = scan_serial(line, settings());
            check(ids_found(r) == std::vector<int>({ 12 }), "a query garbled once is found");
            check_eq(r.stats.garbled, 1, "and the garbled reply counted");
            only_scan_frames(line);
        }
        {
            FakeSerialLine line;
            SerialDevice d = device(12, 444);
            d.garble_range_replies = -1;
            line.devices.push_back(d);

            SerialScanSettings s = settings();
            s.attempts = 5;
            const SerialScanResult r = scan_serial(line, s);
            check(r.devices.empty(), "a device always garbled is not listed");
            check(r.stats.unreadable_ids == std::vector<int>({ 12 }), "its id is reported");
            check_eq(line.queries_for_id(12), 5, "after five tries at it alone, as set");
            check(line.sent.size() < 40, "and the scan ends");
            only_scan_frames(line);
        }
    }

    void test_a_device_that_will_not_be_read()
    {
        section("a device that answers the query but not the read is reported");

        {
            FakeSerialLine line;
            SerialDevice d = device(12, 555);
            d.answers_identity = false;
            line.devices.push_back(d);

            const SerialScanResult r = scan_serial(line, settings());
            check(r.devices.empty(), "silent to the read: not listed");
            check(r.stats.unreadable_ids == std::vector<int>({ 12 }), "its id is reported");
            check_eq(line.reads_of(12), 3, "after three reads");
            only_scan_frames(line);
        }
        {
            FakeSerialLine line;
            SerialDevice d = device(12, 555);
            d.identity_exception = 2;
            line.devices.push_back(d);

            const SerialScanResult r = scan_serial(line, settings());
            check(r.devices.empty(), "refusing the read: not listed");
            check(r.stats.unreadable_ids == std::vector<int>({ 12 }), "its id is reported");
            only_scan_frames(line);
        }
    }

    void test_a_busy_line_is_left_alone()
    {
        section("a line already talking is left alone, with nothing sent");

        {
            FakeSerialLine line;
            line.chatter = { 0x01, 0x03, 0x02, 0x00, 0x05, 0x78, 0x47 };
            line.devices.push_back(device(12, 666));

            const SerialScanResult r = scan_serial(line, settings());
            check(line.sent.empty(), "another master's traffic: nothing sent");
            check_eq(line.receives, 1, "after listening once");
            check(!r.runs_mstp, "it is not MS/TP");
            check(r.line_busy.find("nothing was sent") != std::string::npos, "and the page is told so");
            check(r.devices.empty(), "no devices");
        }
        {
            FakeSerialLine line;
            line.chatter = { 0x55, 0xFF, 0x00, 0x02, 0x01, 0x00, 0x00, 0x7C,
                             0x55, 0xFF, 0x00, 0x03, 0x02, 0x00, 0x00, 0x3B };

            const SerialScanResult r = scan_serial(line, settings());
            check(line.sent.empty(), "MS/TP tokens: nothing sent");
            check(r.runs_mstp, "the line runs MS/TP");
            check(r.line_busy.find("MS/TP") != std::string::npos, "and the page is told so");
        }
        {
            FakeSerialLine line;
            line.runs_mstp = true;

            const SerialScanResult r = scan_serial(line, settings());
            check(r.runs_mstp, "MS/TP answering the first query is MS/TP too");
            check_eq((long)line.sent.size(), 1, "and nothing more is sent after it");
            only_scan_frames(line);
        }
    }

    void test_settings_that_cannot_be_scanned()
    {
        section("settings that cannot be scanned are refused before listening");

        auto refused = [](SerialScanSettings s, const char* what) {
            FakeSerialLine line;
            line.devices.push_back(device(12, 777));
            const SerialScanResult r = scan_serial(line, s);
            check(!r.error.empty(), what);
            check_eq(line.receives, 0, "  nothing was listened for");
            check(line.sent.empty(), "  and nothing sent");
        };

        SerialScanSettings s = settings();
        s.lowest = 0;
        refused(s, "from id 0");

        s = settings();
        s.highest = 255;
        refused(s, "to id 255");

        s = settings();
        s.lowest  = 30;
        s.highest = 20;
        refused(s, "a range upside down");

        s = settings();
        s.attempts = 0;
        refused(s, "no attempts");

        s = settings();
        s.attempts = -1;
        refused(s, "fewer than none");
    }

    void test_a_failing_line_stops_the_scan()
    {
        section("a line that fails stops the scan, and keeps what was found");

        {
            FakeSerialLine line;
            line.fail_receive_at = 0;
            line.devices.push_back(device(12, 888));

            const SerialScanResult r = scan_serial(line, settings());
            check_streq(r.error.c_str(), "the adapter was unplugged", "failing while listening");
            check(line.sent.empty(), "sends nothing");
        }
        {
            FakeSerialLine line;
            line.fail_send_at = 0;
            line.devices.push_back(device(12, 888));

            const SerialScanResult r = scan_serial(line, settings());
            check_streq(r.error.c_str(), "the port was closed", "failing to send");
            check(r.devices.empty(), "finds nothing");
        }
        {
            // Listening is receive 0. Then 1-254 hears both (1), 1-127
            // hears 12 (2), 12 is read (3), and 128-254 would hear 200 (4).
            FakeSerialLine line;
            line.fail_receive_at = 4;
            line.devices.push_back(device(12, 888));
            line.devices.push_back(device(200, 999));

            const SerialScanResult r = scan_serial(line, settings());
            check_streq(r.error.c_str(), "the adapter was unplugged", "failing part way");
            check(ids_found(r) == std::vector<int>({ 12 }), "keeps the device already read");
            check_eq((long)line.sent.size(), 4, "and sends nothing after the failure");
            only_scan_frames(line);
        }
    }

    void test_what_a_device_reports_is_kept()
    {
        section("what a device reports about itself is kept");

        {
            FakeSerialLine line;
            line.devices.push_back(device(12, 0));

            const SerialScanResult r = scan_serial(line, settings());
            if (require(r.devices.size() == 1, "a device with serial 0 is listed"))
            {
                const DeviceRecord& d = r.devices[0];
                if (require(d.repairs.size() == 1, "with one repair"))
                    check(d.repairs[0].kind == RepairKind::AssignSerialNumber, "to give it a serial");
            }
            check_eq((long)line.sent.size(), 2, "and nothing is written to it: a query and a read");
            only_scan_frames(line);
        }
        {
            FakeSerialLine line;
            line.devices.push_back(device(12, 0xFFFFFFFF));

            const SerialScanResult r = scan_serial(line, settings());
            if (require(r.devices.size() == 1, "a device with serial FFFFFFFF is listed"))
                check_eq((long)r.devices[0].repairs.size(), 1, "with the same repair");
            only_scan_frames(line);
        }
        {
            FakeSerialLine line;
            SerialDevice d = device(12, 321);
            d.reported_id = 9;
            line.devices.push_back(d);

            const SerialScanResult r = scan_serial(line, settings());
            if (require(r.devices.size() == 1, "a device whose register 6 disagrees is listed"))
            {
                check_eq(r.devices[0].modbus_id_reported, 9, "with the id it reports");
                check_eq(r.devices[0].connection.modbus_slave_id, 12, "and reached on the id that answered");
            }
        }
        {
            FakeSerialLine line;
            SerialDevice d = device(12, 321);
            d.old_version = 245;
            line.devices.push_back(d);

            const SerialScanResult r = scan_serial(line, settings());
            if (require(r.devices.size() == 1, "an old Tstat is listed"))
                check_eq(r.devices[0].firmware, 245, "with its version from register 4");
        }
    }
}

int run_serial_scan_tests()
{
    test_the_fake_line_speaks_modbus();
    test_a_quiet_line();
    test_one_device_is_found_and_read();
    test_devices_answering_together_are_told_apart();
    test_colliding_replies_are_told_apart();
    test_a_full_range_is_searched_in_the_expected_frames();
    test_only_the_range_asked_is_scanned();
    test_older_firmware_is_found();
    test_a_shared_id_is_reported_and_not_changed();
    test_a_collision_on_one_id_is_unreadable();
    test_noise_is_retried_a_set_number_of_times();
    test_a_device_that_will_not_be_read();
    test_a_busy_line_is_left_alone();
    test_settings_that_cannot_be_scanned();
    test_a_failing_line_stops_the_scan();
    test_what_a_device_reports_is_kept();
    return 0;
}
