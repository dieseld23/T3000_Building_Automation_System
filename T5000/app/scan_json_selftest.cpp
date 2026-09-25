// Tests for the device-list JSON.
//
// These check the payload the page actually consumes, by substring rather than
// by parsing: the point is to catch a field being renamed, dropped, or emitted
// as the wrong JSON type, and a hand-rolled parser here would only test itself.
//
// The properties worth guarding are the ones a reader would rely on and could
// not see going wrong: that a handle is a string, that a repair arrives with
// its consequence attached rather than just its name, and that a device the
// tool cannot vouch for is not described as though it had been read.

#include "scan_json.h"
#include "points_json.h"
#include "../testing/check.h"

#include <string.h>

namespace
{
    using namespace t5000::app;
    using namespace t5000::device;
    using namespace t5000::testing;

    bool has(const std::string& haystack, const char* needle)
    {
        return haystack.find(needle) != std::string::npos;
    }

    DeviceRecord a_device(uint32_t serial)
    {
        DeviceRecord d;
        d.serial_number = serial;
        d.product       = ProductClassId::Tstat10;
        d.firmware      = 538;
        d.address_note  = "192.168.1.50";
        d.provenance    = Provenance::BacnetBroadcast;
        d.reached       = true;
        return d;
    }

    Repair a_repair()
    {
        Repair r;
        r.kind        = RepairKind::AssignSerialNumber;
        r.problem     = "This device reports no serial number.";
        r.action      = "Write a serial to registers 0 and 2.";
        r.consequence = "The device gets a permanent identity.";
        r.reversible  = false;
        return r;
    }

    void test_an_empty_registry_is_not_an_error()
    {
        section("an empty device list is a valid answer, and says which kind");

        Registry reg;
        ScanSummary summary;   // never scanned

        const std::string json = build_devices_json(reg, summary);

        check(has(json, "\"devices\":[]"), "no devices");
        check(has(json, "\"hasScanned\":false"), "and the reason is 'not yet scanned'");
        check(has(json, "\"error\":\"\""), "which is not an error");

        // The page renders four different messages off these three fields.
        // Collapsing them into one "nothing found" is the whole bug this is
        // here to prevent, so the fields have to survive independently.
        summary.has_scanned = true;
        const std::string scanned = build_devices_json(reg, summary);
        check(has(scanned, "\"hasScanned\":true"), "a completed scan is distinguishable");
        check(has(scanned, "\"error\":\"\""), "from a failed one");

        summary.error = "could not send the discovery query";
        const std::string failed = build_devices_json(reg, summary);
        check(has(failed, "could not send the discovery query"), "and the failure is carried");
    }

    void test_a_handle_is_a_string()
    {
        section("handles cross as strings, not numbers");

        Registry reg;
        reg.add_or_merge(a_device(500123));

        const std::string json = build_devices_json(reg, ScanSummary());

        // Quoted. A bare number would work today and round silently if a
        // handle ever passed 2^53 - and this is the key an approval is
        // addressed by, so a rounded one targets a different device.
        check(has(json, "\"handle\":\"1\""), "quoted");
        check(!has(json, "\"handle\":1"), "and not a bare number");
    }

    void test_a_repair_carries_its_consequence()
    {
        section("a repair is disclosed in full, not just named");

        DeviceRecord d = a_device(0);
        d.repairs.push_back(a_repair());

        Registry reg;
        reg.add_or_merge(d);

        const std::string json = build_devices_json(reg, ScanSummary());

        // A repair reduced to its kind is a button with no informed consent
        // behind it. What would be written, and what happens afterwards, have
        // to reach the operator.
        check(has(json, "assign a serial number"), "the kind");
        check(has(json, "reports no serial number"), "the problem");
        check(has(json, "registers 0 and 2"), "exactly what would be written");
        check(has(json, "permanent identity"), "and what that does");
        check(has(json, "\"reversible\":false"), "and whether it can be undone");
        check(has(json, "\"approved\":false"), "unapproved");
        check(has(json, "\"needsAttention\":true"), "the device is flagged");
        check(has(json, "\"pendingRepairs\":1"), "and counted");

        // Nothing has been sent. The payload says so rather than leaving the
        // page to assert it from a hard-coded string.
        check(has(json, "\"readOnly\":true"), "the read-only claim travels with the data");
    }

    void test_an_unidentified_device_is_reported_as_such()
    {
        section("a device with no serial is not dressed up as identified");

        Registry reg;
        reg.add_or_merge(a_device(0));
        reg.add_or_merge(a_device(0xFFFFFFFFu));

        const std::string json = build_devices_json(reg, ScanSummary());

        check(has(json, "\"hasStableIdentity\":false"), "flagged as unidentified");
        check(has(json, "\"unidentifiedCount\":2"), "both of them counted");

        // 0xFFFFFFFF must arrive as 4294967295, not -1. The signed version of
        // this field is what merged two nameless devices into one record.
        check(has(json, "\"serialNumber\":4294967295"), "the all-FF serial is unsigned");
        check(!has(json, "\"serialNumber\":-1"), "and is not -1");
    }

    void test_the_panel_type_is_not_invented()
    {
        section("a panel type of 0 is resolved, not assumed");

        // The bug this guards was found by running the server, not by a test:
        // a TSTAT10 reporting mini_type 0 was shown CM5's eighteen inputs,
        // because 0 is a real panel type for a CM5 and "unset" for everything
        // else.
        Registry reg;
        DeviceRecord tstat = a_device(600001);
        tstat.product   = ProductClassId::Tstat10;
        tstat.mini_type = 0;
        reg.add_or_merge(tstat);

        const std::string json = build_devices_json(reg, ScanSummary());

        check(has(json, "\"resolved\":false"), "the panel type is unresolved");
        check(has(json, "\"reason\":\""), "with a reason a person can read");
        check(has(json, "not a CM5"), "naming why 0 does not mean CM5 here");
    }

    void test_a_stale_handle_resolves_to_null()
    {
        section("a detail request for a device that is gone returns null");

        Registry reg;
        reg.add_or_merge(a_device(700001));
        const Handle live = reg.devices()[0].handle;

        check(build_device_json(reg, live) != "null", "a live handle resolves");
        check(build_device_json(reg, to_handle(9999)) == "null", "a stale one does not");
        check(build_device_json(reg, kNoHandle) == "null", "nor does the null handle");

        // Null rather than the first device, or an empty object. A page
        // holding a handle for a device that has gone needs to be told that,
        // not handed a plausible substitute.
        reg.clear();
        check(build_device_json(reg, live) == "null", "and it stops resolving after a clear");
    }

    void test_selection_is_marked_on_exactly_one_device()
    {
        section("the selected device is marked, and only that one");

        Registry reg;
        reg.add_or_merge(a_device(800001));
        reg.add_or_merge(a_device(800002));

        const std::string none = build_devices_json(reg, ScanSummary());
        check(has(none, "\"selectedHandle\":\"\""), "nothing selected reads as empty");
        check(!has(none, "\"selected\":true"), "and no device claims to be");

        reg.select_by_handle(reg.devices()[1].handle);
        const std::string one = build_devices_json(reg, ScanSummary());
        check(has(one, "\"selectedHandle\":\"2\""), "the handle is reported");

        // Exactly one, not "at least one".
        size_t count = 0, at = 0;
        while ((at = one.find("\"selected\":true", at)) != std::string::npos) { count++; at++; }
        check_eq((long)count, 1, "exactly one device is marked selected");
    }

    void test_text_from_a_device_is_escaped()
    {
        section("device text cannot break out of the JSON");

        // Panel names come off the wire and are not trusted. A quote or a
        // backslash in one must not end the string it is sitting in.
        Registry reg;
        DeviceRecord d = a_device(900001);
        d.address_note = "192.168.1.9 (Say \"hi\")";
        reg.add_or_merge(d);

        const std::string json = build_devices_json(reg, ScanSummary());
        check(has(json, "Say \\\"hi\\\""), "quotes are escaped");
        check(has(json, "\"readOnly\":true"), "and the document is still intact to the end");
    }

    void test_the_unreadable_device_payload_keeps_the_page_contract()
    {
        section("a device that cannot be read never reads as one that was");

        // A regression, and the worst kind this project can produce.
        //
        // /api/inputs used to answer a selected real device with a payload
        // that had "points" instead of "inputs" and no isFixture at all. The
        // page reads data.inputs (so the grid was empty) and treats a missing
        // isFixture as false - and renders false as "Live device. Points
        // below were read from serial NNN".
        //
        // So the screen asserted a reading that never happened, in the same
        // commit that stopped it serving fixture points for a real device.
        // Caught by loading the page, not by the tests or the compiler.
        const std::string json = build_unavailable_inputs_json(
            700002, "192.168.1.51 (Boiler Room)", "The read path is not verified.");

        // Every field the page reads unconditionally, present.
        check(has(json, "\"unavailable\":true"), "the state is stated outright");
        check(has(json, "\"isFixture\":false"),
              "isFixture is PRESENT and false - absent is what caused the lie");
        check(has(json, "\"inputs\":[]"),
              "the array is named inputs, which is what the page reads");
        check(has(json, "\"readPath\""), "a read path band is always rendered");
        check(has(json, "nothing was read"), "and says nothing was read");
        check(has(json, "\"count\":0"), "nothing was read");
        check(has(json, "The read path is not verified."), "the reason reaches the page");
        check(has(json, "700002"), "the device is named");
        check(has(json, "Boiler Room"), "with its address");

        // The field name that caused it. "points" is what the old payload
        // used, and the page has never read it.
        check(!has(json, "\"points\""), "no stray points array to be ignored");

        // And the claim itself must not be constructible from this payload.
        check(!has(json, "\"isFixture\":true"), "it is not fixture data either");
        check(has(json, "\"readFromWire\":false"),
              "and it says, positively, that nothing came off the wire");

        // The panel and custom range keys a read payload carries, present
        // and empty: a page reading data.panel.known must find false, not
        // undefined.
        check(has(json, "\"panel\":{\"known\":false"), "the panel is there, and not known");
        check(has(json, "\"inputsShown\":0"), "with nothing shown");
        check(has(json, "\"customRanges\":{\"digitalKnown\":false"), "and no custom range names");

        // The page reads device.sighting in both of its banners.
        check(has(json, "\"sighting\":\"\""), "the sighting is there, and empty when none was given");
        const std::string unseen = build_unavailable_inputs_json(700002, "192.168.1.51", "Not read.",
                                                                 "Not seen since T5000 started.");
        check(has(unseen, "\"sighting\":\"Not seen since T5000 started.\""), "  and carried when it is");
    }

    void test_the_parent_reaches_the_page()
    {
        section("a device's parent controller is in the device list");

        Registry reg;
        DeviceRecord child;
        child.serial_number = 900030;
        child.parent_serial = 500001;
        reg.add_or_merge(child);
        const std::string json = build_devices_json(reg, ScanSummary());
        check(has(json, "\"parentSerial\":500001"), "parentSerial is emitted");
    }

    void test_both_addresses_reach_the_page()
    {
        section("the address it answered from and the one it reports are both in the list");

        Registry reg;
        DeviceRecord d;
        d.serial_number        = 900040;
        d.observation_complete = true;
        d.answered_from        = "10.1.2.3";
        d.reported_ip          = "192.168.1.50";
        reg.add_or_merge(d);

        DeviceRecord same;
        same.serial_number        = 900041;
        same.observation_complete = true;
        same.answered_from        = "192.168.1.51";
        same.reported_ip          = "192.168.1.51";
        reg.add_or_merge(same);

        const std::string json = build_devices_json(reg, ScanSummary());
        check(has(json, "\"answeredFrom\":\"10.1.2.3\""), "answeredFrom is emitted");
        check(has(json, "\"reportedIp\":\"192.168.1.50\""), "reportedIp is emitted");
        check(has(json, "\"addressMismatch\":true"), "a disagreement is flagged");
        check(has(json, "\"addressMismatch\":false"), "and agreement is not");
    }

    void test_only_a_wire_read_claims_one()
    {
        section("only a payload built from a wire read says it was read from the device");

        // The page claims "read from serial NNN" on readFromWire and nothing
        // else. Every builder states it, so no payload can make the claim by
        // leaving a field out.
        t5000::device::Decision decision;
        decision.summary = "private data";

        DeviceInfo fixture;
        fixture.serial_number = 1;
        fixture.is_fixture    = true;
        const std::string f = build_inputs_json(fixture, decision, {});
        check(has(f, "\"readFromWire\":false"), "fixture points: false");

        DeviceInfo defaulted;
        defaulted.serial_number = 2;
        const std::string d = build_inputs_json(defaulted, decision, {});
        check(has(d, "\"readFromWire\":false"),
              "a DeviceInfo nobody set the flag on: false, not true by omission");

        DeviceInfo live;
        live.serial_number  = 700003;
        live.read_from_wire = true;
        live.address        = "192.168.1.52:47808";
        const std::string l = build_inputs_json(live, decision, {});
        check(has(l, "\"readFromWire\":true"), "a wire read: true");
        check(has(l, "192.168.1.52:47808"), "with the address it was read from");
        check(has(l, "\"sighting\":\"\""), "and a sighting, empty for a device the scan found");

        live.sighting = "Not seen \"since\" T5000 started.";
        check(has(build_inputs_json(live, decision, {}), "\"sighting\":\"Not seen \\\"since\\\" T5000 started.\""),
              "  escaped, when there is one");
    }

    void test_inputs_carry_what_t3000_shows()
    {
        section("each input carries T3000's text for its columns, and the raw fields");

        t5000::wire::InputPoint temp{};
        temp.digital_analog = 1;
        temp.range          = 3;
        temp.value          = 21500;
        temp.decom          = 0x11;   // open circuit, 4-20 ma

        t5000::wire::InputPoint custom{};
        custom.digital_analog = 0;
        custom.range          = 23;
        custom.control        = 1;

        DeviceInfo device;
        device.serial_number = 700010;
        t5000::device::Decision decision;
        decision.summary = "private data";

        const std::string j = build_inputs_json(device, decision, { temp, custom });
        check(has(j, "\"value\":\"21.50\""), "the value as T3000 formats it");
        check(has(j, "\"units\":\"\xC2\xB0" "C\""), "units, in UTF-8");
        check(has(j, "\"range\":\"10K Type2\""), "the range's name");
        check(has(j, "\"status\":\"Open\""), "the status text");
        check(has(j, "\"alarm\":true"), "flagged as an alarm");
        check(has(j, "\"signalType\":\"4-20 ma\""), "the signal type");
        check(has(j, "custom digital range 1"), "a note where T5000 cannot show what T3000 would");
        check(has(j, "\"raw\":{\"value\":21500,\"range\":3"), "and the raw fields it came from");
        check(has(j, "\"panel\":{\"known\":false"), "and the panel's settings are said to be unknown");
    }

    void test_interfaces_report_their_failure()
    {
        section("the interface list distinguishes empty from broken");

        std::vector<t5000::net::Interface> none;
        const std::string empty = build_interfaces_json(none, std::string());
        check(has(empty, "\"interfaces\":[]"), "no interfaces");
        check(has(empty, "\"error\":\"\""), "and no error");

        const std::string broken = build_interfaces_json(none, "could not list network interfaces");
        check(has(broken, "could not list network interfaces"), "a failure is reported");

        t5000::net::Interface n;
        n.ip = "192.168.1.23";
        n.name = "Ethernet";
        n.description = "Intel I219-V";
        n.is_up = true;
        const std::string one = build_interfaces_json({ n }, std::string());
        check(has(one, "192.168.1.23"), "the address");
        check(has(one, "Ethernet"), "the name a person recognises");
        check(has(one, "\"isUp\":true"), "and whether it is up");
    }
}

namespace
{
    void test_the_saved_list_reaches_the_page()
    {
        section("the saved list's fields reach the page, and whether each device answered");

        Registry reg;
        const int scan = reg.begin_scan();

        DeviceRecord now = a_device(8001);
        now.answered_scan = scan;
        now.first_seen    = 100;
        now.last_seen     = 200;
        now.panel_name    = "AHU";
        reg.add_or_merge(now);

        Placement p;
        p.name     = "Boiler \"B\"";
        p.building = "North";
        p.floor    = "2";
        p.room     = "Plant";
        reg.set_placement(reg.devices()[0].handle, p);

        DeviceRecord before = a_device(8002);
        before.provenance = Provenance::Restored;
        before.last_seen  = 50;
        reg.add_or_merge(before);

        StoreStatus store;
        store.saving   = true;
        store.path     = "C:\\T5000\\T5000.db";
        store.restored = 1;

        const std::string json = build_devices_json(reg, ScanSummary(), store);

        const std::string first  = json.substr(0, json.find("\"serialNumber\":8002"));
        const std::string second = json.substr(json.find("\"serialNumber\":8002"));

        check(has(first, "\"panelName\":\"AHU\""), "the panel's own name");
        check(has(first, "\"placement\":{\"name\":\"Boiler \\\"B\\\"\",\"building\":\"North\","
                         "\"floor\":\"2\",\"room\":\"Plant\"}"),
              "the operator's name and location, escaped");
        check(has(first, "\"firstSeen\":100"), "when it was first seen");
        check(has(first, "\"lastSeen\":200"), "and last seen, as numbers");
        check(has(first, "\"answeredLastScan\":true"), "that it answered the last scan");
        check(has(first, "\"seenThisSession\":true"), "and so was seen this session");

        check(has(second, "\"answeredLastScan\":false"), "the restored one did not answer");
        check(has(second, "\"seenThisSession\":false"), "and has not been seen this session");
        check(has(second, "\"provenance\":\"restored from the saved list\""), "and says where it came from");

        check(has(json, "\"scanCount\":1"), "how many scans have run");
        check(has(json, "\"store\":{\"saving\":true,\"path\":\"C:\\\\T5000\\\\T5000.db\","
                        "\"error\":\"\",\"restored\":1}"),
              "and whether, and where, the list is saved");
    }

    void test_a_list_not_being_saved_says_why()
    {
        section("a list that is not being saved says so, and why");

        Registry reg;
        StoreStatus store;
        store.path  = "D:\\T5000.db";
        store.error = "unable to open database file";

        const std::string json = build_devices_json(reg, ScanSummary(), store);
        check(has(json, "\"saving\":false"), "not saving");
        check(has(json, "\"error\":\"unable to open database file\""), "and the reason");

        const std::string defaulted = build_devices_json(reg, ScanSummary());
        check(has(defaulted, "\"saving\":false"), "a caller that passes no status claims no saving");
    }
}

int run_scan_json_tests()
{
    test_an_empty_registry_is_not_an_error();
    test_a_handle_is_a_string();
    test_a_repair_carries_its_consequence();
    test_an_unidentified_device_is_reported_as_such();
    test_the_panel_type_is_not_invented();
    test_a_stale_handle_resolves_to_null();
    test_selection_is_marked_on_exactly_one_device();
    test_text_from_a_device_is_escaped();
    test_the_unreadable_device_payload_keeps_the_page_contract();
    test_only_a_wire_read_claims_one();
    test_the_parent_reaches_the_page();
    test_both_addresses_reach_the_page();
    test_interfaces_report_their_failure();
    test_inputs_carry_what_t3000_shows();
    test_the_saved_list_reaches_the_page();
    test_a_list_not_being_saved_says_why();
    return 0;
}
