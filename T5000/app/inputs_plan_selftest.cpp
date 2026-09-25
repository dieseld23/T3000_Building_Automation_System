// Tests for deciding whether a device's Inputs are read at all.

#include "inputs_plan.h"
#include "../discovery/scanner.h"
#include "../testing/check.h"
#include "../testing/fake_transport.h"

namespace
{
    using namespace t5000::app;
    using namespace t5000::device;
    using namespace t5000::testing;

    // 2026-09-21 14:13:20 UTC, and within a day of that in any time zone.
    constexpr int64_t kLastSeen = 1790000000;

    // A device that answered this session's first scan.
    DeviceRecord scanned(ProductClassId product, const char* host = "192.168.1.50", int port = 47808)
    {
        DeviceRecord d;
        d.serial_number        = 800100;
        d.product              = product;
        d.firmware             = 600;
        d.connection.transport = Transport::BacnetIp;
        d.connection.host      = host;
        d.connection.udp_port  = port;
        d.provenance           = Provenance::BacnetBroadcast;
        d.answered_scan        = 1;
        d.last_seen            = kLastSeen;
        return d;
    }

    // The same device as the saved list gives it back after a restart: no
    // scan this session has found it.
    DeviceRecord restored(ProductClassId product = ProductClassId::Cm5)
    {
        DeviceRecord d = scanned(product);
        d.provenance    = Provenance::Restored;
        d.answered_scan = 0;
        return d;
    }

    bool has(const std::string& text, const std::string& part)
    {
        return text.find(part) != std::string::npos;
    }

    void test_a_private_data_controller_is_read()
    {
        section("a scanned private-data controller is read, at the address its scan gave");

        const InputsPlan p = plan_inputs_read(scanned(ProductClassId::Cm5, "10.1.2.3", 47809));
        check(p.can_read, "a CM5 on BACnet/IP can be read");
        check(p.endpoint.ip == 0x0A010203 && p.endpoint.port == 47809,
              "at the scanned address and port, not a default");
        check(p.decision.path == ReadPath::PrivateData, "by private data");
        check(p.note.empty(), "with nothing to add");
    }

    void test_everything_else_is_refused_with_a_reason()
    {
        section("every device that is not read says why");

        const InputsPlan tstat8 = plan_inputs_read(scanned(ProductClassId::Tstat8));
        check(!tstat8.can_read, "a Tstat8 - not a private-data product - is not sent a request");
        check(tstat8.reason.find("does not support private-data") != std::string::npos &&
              tstat8.reason.find("does not read Modbus registers yet") != std::string::npos,
              "  and says both that it is the product and what T5000 lacks");

        DeviceRecord serial = scanned(ProductClassId::Cm5);
        serial.connection.transport = Transport::ModbusRtu;
        const InputsPlan s = plan_inputs_read(serial);
        check(!s.can_read && s.reason.find("BACnet/IP only") != std::string::npos,
              "a serial connection is not read, and says so");

        const InputsPlan nohost = plan_inputs_read(scanned(ProductClassId::Cm5, ""));
        check(!nohost.can_read && nohost.reason.find("did not report an address") != std::string::npos,
              "no address, no request");

        const InputsPlan badhost = plan_inputs_read(scanned(ProductClassId::Cm5, "controller.local"));
        check(!badhost.can_read, "a hostname is not an address T5000 sends to");

        const InputsPlan badport = plan_inputs_read(scanned(ProductClassId::Cm5, "10.1.2.3", 0));
        check(!badport.can_read, "nor is port 0");
    }

    void test_a_device_behind_a_controller_is_not_read()
    {
        section("a device a controller answered for is never sent a read");

        // A TSTAT10 on a T3-BB's RS485 bus: a private-data product, on
        // BACnet/IP by every other test here - and at the T3-BB's address.
        DeviceRecord child = scanned(ProductClassId::Tstat10);
        child.parent_serial = 500001;

        const InputsPlan p = plan_inputs_read(child);
        check(!p.can_read, "not read");
        check(p.reason.find("500001") != std::string::npos,
              "  names the controller it is behind");
        check(p.reason.find("controller's inputs") != std::string::npos,
              "  and says what a read would actually return");

        child.product = ProductClassId::Cm5;
        check(!plan_inputs_read(child).can_read, "whatever the product");
    }

    void test_a_device_behind_a_controller_is_not_read_from_the_address_it_came_from()
    {
        section("a sub-device stays refused now that the address is the one it answered from");

        // The controller answers for its sub-device, so the sub-device's
        // response arrives FROM the controller. Using the sender address
        // makes that explicit - the host is now certainly the controller's -
        // and the refusal must not depend on which address was chosen.
        t5000::discovery::ScanResponse r;
        r.serial_number        = 800200;
        r.product_id           = static_cast<uint8_t>(ProductClassId::Cm5);
        r.parent_serial_number = 800100;
        r.ip[0] = 192; r.ip[1] = 168; r.ip[2] = 1; r.ip[3] = 77;
        r.bacnet_port          = 47808;

        const DeviceRecord child = t5000::discovery::to_record(r, 0xC0A80132);   // from 192.168.1.50
        check(child.connection.host == "192.168.1.50", "its host is the controller's address");

        const InputsPlan p = plan_inputs_read(child);
        check(!p.can_read, "and it is still not read");
        check(p.reason.find("800100") != std::string::npos, "  naming the controller");
    }

    void test_an_esp32_is_read()
    {
        section("an ESP32 T3 is read, with its count left to its settings");

        const InputsPlan p = plan_inputs_read(scanned(ProductClassId::Esp32T3Series));
        check(p.can_read, "an ESP32 T3 is read");
        check(p.note.find("more than 64") == std::string::npos,
              "and the plan no longer claims only 64 are shown - the settings decide that now");
    }

    // ------------------------------------------ seen this session, or not

    void test_a_device_seen_this_session_is_vouched_for()
    {
        section("a device that answered a scan this session is read as before, with nothing said of it");

        const InputsPlan p = plan_inputs_read(scanned(ProductClassId::Cm5));
        check(p.can_read, "it is read");
        check(p.seen_this_session, "as seen this session");
        check(p.identity == Identity::VouchedForByScan, "its scan vouches for the panel at the address");
        check(p.sighting.empty(), "and the page is told nothing about when it was seen");

        DeviceRecord later = scanned(ProductClassId::Cm5);
        later.answered_scan = 3;
        check(plan_inputs_read(later).identity == Identity::VouchedForByScan, "whichever scan it answered");
    }

    void test_a_restored_device_must_confirm_its_serial()
    {
        section("a device known only from the saved list must confirm its serial, and the page says why");

        const InputsPlan p = plan_inputs_read(restored());
        check(p.can_read, "it is still read");
        check(!p.seen_this_session, "as not seen this session");
        check(p.identity == Identity::MustConfirm, "on the condition that its settings confirm its serial");
        check(has(p.sighting, "Not seen since T5000 started"), "the page is told it has not been seen");
        check(has(p.sighting, "last seen, " + local_time_text(kLastSeen) + "."), "  and when it last was");

        DeviceRecord never = restored();
        never.last_seen = 0;
        const InputsPlan n = plan_inputs_read(never);
        check(n.identity == Identity::MustConfirm, "with no time saved: the same rule");
        check(has(n.sighting, "Not seen since T5000 started") && has(n.sighting, "saved for it."),
              "  and the page is told it has not been seen");
        check(!has(n.sighting, "last seen"), "  without a time it does not have");

        check(InputsPlan().identity == Identity::MustConfirm,
              "a plan that says nothing about sightings is held to the stricter rule");
    }

    void test_a_restored_device_the_plan_refuses_still_says_when_it_was_seen()
    {
        section("a device from the saved list that is not read still says it has not been seen");

        DeviceRecord serial = restored();
        serial.connection.transport = Transport::ModbusRtu;
        const InputsPlan s = plan_inputs_read(serial);
        check(!s.can_read, "a serial connection is not read");
        check(has(s.sighting, "Not seen since T5000 started"), "  and the sighting is on the plan");

        DeviceRecord child = restored(ProductClassId::Tstat10);
        child.parent_serial = 500001;
        const InputsPlan c = plan_inputs_read(child);
        check(!c.can_read, "a device behind a controller is not read");
        check(has(c.sighting, "Not seen since T5000 started"), "  and the sighting is on the plan");
        check(c.identity == Identity::MustConfirm, "  with its identity");
    }

    // ------------------------------------------------------ added by hand

    // An entry as app::add_device makes one: the operator's serial and
    // product, and nothing a scan learned.
    DeviceRecord added_by_hand(ProductClassId product = ProductClassId::Cm5)
    {
        DeviceRecord d;
        d.serial_number = 800300;
        d.product       = product;
        d.provenance    = Provenance::ManuallyAdded;
        return d;
    }

    void test_a_device_added_by_hand_is_not_read()
    {
        section("a device added by hand that no scan has found is sent nothing");

        const InputsPlan p = plan_inputs_read(added_by_hand());
        check(!p.can_read, "it is not read");
        check(has(p.reason, "There is no device to read"), "  the page is told there is nothing to read");
        check(has(p.reason, "added by hand") && has(p.reason, "800300"), "  and why, by serial");
        check(has(p.reason, "Nothing was sent"), "  and that nothing was sent");
        check(p.sighting.empty(), "  with no note about a saved address it does not have");

        // Whatever else is on the record. An address, a sighting, or a
        // product T5000 reads does not make an entry someone typed in into a
        // device that has answered.
        DeviceRecord with_host = added_by_hand(ProductClassId::Esp32T3Series);
        with_host.connection.host     = "127.0.0.1";
        with_host.connection.udp_port = 47900;
        with_host.firmware            = 600;
        with_host.last_seen           = kLastSeen;
        check(!plan_inputs_read(with_host).can_read, "not even with an address and a sighting on it");

        check(!plan_inputs_read(DeviceRecord()).can_read,
              "a record nobody gave a provenance is refused the same way");
    }

    void test_a_device_added_by_hand_is_read_once_a_scan_finds_it()
    {
        section("once a scan finds its serial, a device added by hand is read like any other");

        Registry reg;
        const int i = reg.add_or_merge(added_by_hand());

        DeviceRecord found          = scanned(ProductClassId::Cm5, "10.1.2.3", 47809);
        found.serial_number         = 800300;
        found.reached               = true;
        found.observation_complete  = true;
        reg.add_or_merge(found);

        if (!require(reg.size() == 1, "the scan's device takes the entry's place"))
            return;

        const InputsPlan p = plan_inputs_read(reg.devices()[i]);
        check(p.can_read, "it is read");
        check(p.identity == Identity::VouchedForByScan, "  vouched for by the scan that found it");
        check(p.endpoint.ip == 0x0A010203 && p.endpoint.port == 47809, "  at the address the scan gave");
    }

    void test_a_record_from_the_scan_is_never_taken_for_one_added_by_hand()
    {
        section("a record the scan builds is never refused as added by hand");

        t5000::discovery::ScanResponse r;
        r.serial_number    = 800400;
        r.product_id       = static_cast<uint8_t>(ProductClassId::Cm5);
        r.software_version = 600;
        r.ip[0] = 10; r.ip[1] = 1; r.ip[2] = 2; r.ip[3] = 3;
        r.bacnet_port      = 47808;

        DeviceRecord d  = t5000::discovery::to_record(r, 0x0A010203);
        d.answered_scan = 1;

        const InputsPlan p = plan_inputs_read(d);
        check(p.can_read, "it is read");
        check(!has(p.reason, "added by hand"), "  and nothing calls it added by hand");
    }

    void test_times_are_shown_to_the_minute()
    {
        section("a time is shown as date and minute, in this computer's time zone");

        const std::string t = local_time_text(kLastSeen);
        check_eq((long)t.size(), 16, "YYYY-MM-DD HH:MM, sixteen characters");
        check(t.compare(0, 9, "2026-09-2") == 0, "the day, in any time zone");
        check(t.size() == 16 && t[4] == '-' && t[7] == '-' && t[10] == ' ' && t[13] == ':',
              "the separators where they belong");
        check(local_time_text(kLastSeen + 39) == t, "seconds are not shown");
        check(local_time_text(kLastSeen + 60) != t, "minutes are");
        check(local_time_text(0).empty(), "0, never seen: nothing");
        check(local_time_text(-1).empty(), "a time before 1970: nothing");
    }

    // ---------------------------------------------------------- the payload

    InputsPageRead read_ok()
    {
        InputsPageRead r;
        r.ok     = true;
        r.points = std::vector<t5000::wire::InputPoint>(3);
        return r;
    }

    void test_the_payload_for_a_device_seen_this_session()
    {
        section("the payload for a device the scan found says nothing about the saved list");

        const DeviceRecord d = scanned(ProductClassId::Cm5, "10.1.2.3", 47809);
        InputsPlan plan = plan_inputs_read(d);
        plan.note = "A NOTE FROM THE PLAN.";
        const std::string json = inputs_payload(d, plan, read_ok());

        check(has(json, "\"readFromWire\":true"), "it was read from the device");
        check(has(json, "\"address\":\"10.1.2.3:47809\""), "at the address the plan chose");
        check(has(json, "\"sighting\":\"\""), "with no sighting");
        check(!has(json, "the one saved for it"), "and no word of the saved serial");
        check(has(json, "A NOTE FROM THE PLAN."), "the plan's note is carried to the read path");
        check(has(json, "\"count\":3"), "and the points");
    }

    void test_the_payload_for_a_restored_device()
    {
        section("the payload for a device from the saved list says it was not seen, and whether it was confirmed");

        const DeviceRecord d = restored();
        const InputsPlan plan = plan_inputs_read(d);

        const std::string read = inputs_payload(d, plan, read_ok());
        check(has(read, "\"readFromWire\":true"), "read: from the device");
        check(has(read, "Not seen since T5000 started"), "  with the sighting");
        check(has(read, "serial 800100, the one saved for it, so it is the same device"),
              "  and that its settings confirmed the saved serial");

        InputsPageRead refused;
        refused.error = "THE READ'S OWN REASON.";
        const std::string none = inputs_payload(d, plan, refused);
        check(has(none, "\"unavailable\":true") && has(none, "\"readFromWire\":false"),
              "not read: nothing from the device");
        check(has(none, "\"message\":\"THE READ'S OWN REASON.\""), "  the read's reason as the message");
        check(has(none, "Not seen since T5000 started"), "  and the sighting beside it");
        check(!has(none, "the same device"), "  with no claim that it was confirmed");
    }

    void test_the_read_is_held_to_the_plans_identity()
    {
        section("a planned read is held to the plan's identity, not one chosen where it is carried out");

        // A panel that refuses every read. The scan vouches for a device it
        // found, so a refused settings read is noted and the rest is asked
        // for; a device from the saved list has nothing after it.
        const auto refuses = [](const FakeTransport::Sent& s, size_t, FakeTransport& t)
        {
            t.reply(refusal(s.invoke_id));
        };
        uint8_t invoke = 0;

        const DeviceRecord seen = scanned(ProductClassId::Cm5);
        FakeTransport a;
        a.respond = refuses;
        read_planned_inputs(seen, plan_inputs_read(seen), a, instant(), invoke);
        check(a.sent.size() > 1, "seen this session: the names and inputs are still asked for");

        const DeviceRecord saved = restored();
        FakeTransport b;
        b.respond = refuses;
        const std::string json = read_planned_inputs(saved, plan_inputs_read(saved), b, instant(), invoke);
        check_eq((long)b.sent.size(), 1, "from the saved list: the settings, and nothing after");
        check(has(json, "\"unavailable\":true") && has(json, "Scan, and then open Inputs again"),
              "  and the page is told to scan first");
        check(has(json, "Not seen since T5000 started"), "  with the sighting");
    }
}

int run_inputs_plan_tests()
{
    test_a_private_data_controller_is_read();
    test_everything_else_is_refused_with_a_reason();
    test_a_device_behind_a_controller_is_not_read();
    test_a_device_behind_a_controller_is_not_read_from_the_address_it_came_from();
    test_an_esp32_is_read();
    test_a_device_seen_this_session_is_vouched_for();
    test_a_restored_device_must_confirm_its_serial();
    test_a_restored_device_the_plan_refuses_still_says_when_it_was_seen();
    test_a_device_added_by_hand_is_not_read();
    test_a_device_added_by_hand_is_read_once_a_scan_finds_it();
    test_a_record_from_the_scan_is_never_taken_for_one_added_by_hand();
    test_times_are_shown_to_the_minute();
    test_the_payload_for_a_device_seen_this_session();
    test_the_payload_for_a_restored_device();
    test_the_read_is_held_to_the_plans_identity();
    return 0;
}
