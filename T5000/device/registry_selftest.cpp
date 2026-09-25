// Tests for the device registry and the staged-repair model.
//
// The repair tests matter most. T5000 diverges from T3000 deliberately here -
// T3000 writes to devices during a scan, T5000 proposes and waits - and a
// divergence that is only a comment is a divergence one refactor away from
// being undone. These assert the waiting.

#include "registry.h"
#include "../testing/check.h"

namespace
{
    using namespace t5000::device;
    using namespace t5000::testing;

    DeviceRecord a_device(uint32_t serial, ProductClassId product = ProductClassId::Tstat10)
    {
        DeviceRecord d;
        d.serial_number = serial;
        d.product       = product;
        d.provenance    = Provenance::BacnetBroadcast;
        d.reached       = true;
        return d;
    }

    // The handle the registry gave the device now sitting at `index`. Tests
    // that want to act on "the device I just added" go through this rather
    // than passing the index, because the API no longer accepts an index and
    // deliberately will not compile if one is passed.
    Handle handle_at(const Registry& reg, int index)
    {
        return reg.devices()[index].handle;
    }

    Repair a_repair(RepairKind kind = RepairKind::AssignSerialNumber)
    {
        Repair r;
        r.kind        = kind;
        r.problem     = "the device reports serial 0";
        r.action      = "write a serial number to register 255";
        r.consequence = "the device becomes distinguishable from others reporting 0";
        return r;
    }

    void test_repairs_start_unapproved()
    {
        section("a proposed repair is not approved by existing");

        // The entire safety property in one check: noticing a problem must
        // never be the same event as deciding to fix it.
        const Repair r = a_repair();
        check(!r.approved, "a freshly constructed repair is not approved");

        DeviceRecord d = a_device(500123);
        d.repairs.push_back(a_repair());

        Registry reg;
        reg.add_or_merge(d);

        check_eq((int)reg.pending_repairs().size(), 1, "and is pending in the registry");
        check(reg.devices()[0].needs_attention(), "the device is flagged");
    }

    void test_approval_is_per_repair_and_per_device()
    {
        section("approving one repair approves only that one");

        DeviceRecord a = a_device(1001);
        a.repairs.push_back(a_repair(RepairKind::AssignSerialNumber));
        a.repairs.push_back(a_repair(RepairKind::ResolveDuplicateModbusId));

        DeviceRecord b = a_device(1002);
        b.repairs.push_back(a_repair(RepairKind::AssignSerialNumber));

        Registry reg;
        const int ia = reg.add_or_merge(a);
        const int ib = reg.add_or_merge(b);

        check_eq((int)reg.pending_repairs().size(), 3, "three repairs pending");

        check(reg.approve_repair(handle_at(reg, ia), 0),
              "approve the first repair on the first device");

        // The other repair on the SAME device must still be pending, and so
        // must the same KIND of repair on a different device. Blanket
        // approval is how "fix the serial on this one" becomes "renumber the
        // building".
        check_eq((int)reg.pending_repairs().size(), 2, "only one became approved");
        check(reg.devices()[ia].repairs[1].approved == false,
              "the other repair on that device is untouched");
        check(reg.devices()[ib].repairs[0].approved == false,
              "the same repair on another device is untouched");
        check(reg.devices()[ia].needs_attention(), "the device still needs attention");
    }

    void test_approval_does_not_survive_a_rescan()
    {
        section("a rescan withdraws approvals");

        DeviceRecord d = a_device(2001);
        d.repairs.push_back(a_repair());

        Registry reg;
        const int i = reg.add_or_merge(d);
        check(reg.approve_repair(handle_at(reg, i), 0), "approved");
        check_eq((int)reg.pending_repairs().size(), 0, "nothing pending");

        // The same device seen again, with a fresh view of its problems. The
        // operator approved a write based on what was known then; the tool
        // must not carry that consent forward onto a new observation.
        DeviceRecord again = a_device(2001);
        again.repairs.push_back(a_repair());
        reg.add_or_merge(again);

        check_eq(reg.size(), 1, "still one device");
        check_eq((int)reg.pending_repairs().size(), 1, "approval was withdrawn");
    }

    void test_stale_keys_are_refused_not_clamped()
    {
        section("approvals for a device that is not here are refused");

        // A page held open across a rescan sends keys that no longer mean
        // what they did. Clamping would approve a WRITE on whichever device
        // happened to be last in the list.
        Registry reg;
        reg.add_or_merge(a_device(3001));
        const Handle real = handle_at(reg, 0);

        check(!reg.approve_repair(real, 0), "no such repair on a real device");
        check(!reg.approve_repair(to_handle(9999), 0), "no such device");
        check(!reg.approve_repair(kNoHandle, 0), "the null handle");
        check(!reg.approve_repair(real, -1), "negative repair index");

        // Handle 1 is a real handle here, and an index of 1 would be out of
        // range on a one-device list. The two key spaces are not
        // interchangeable, which is why Handle is its own type - passing an
        // index to any of these calls is a compile error, not a wrong write.
        check_eq((int)to_number(real), 1, "the first handle issued is 1");
    }

    void test_merge_only_on_serial()
    {
        section("devices merge on serial number and nothing else");

        Registry reg;
        reg.add_or_merge(a_device(4001));
        reg.add_or_merge(a_device(4001));
        check_eq(reg.size(), 1, "the same serial merges");

        reg.add_or_merge(a_device(4002));
        check_eq(reg.size(), 2, "a different serial does not");
    }

    void test_unidentified_devices_never_merge()
    {
        section("devices reporting serial 0 stay separate");

        // Merging these would turn "there are four devices out there I cannot
        // tell apart" into "there is one device", which is the opposite of
        // the truth and hides a site-wide problem.
        Registry reg;
        for (int i = 0; i < 4; i++)
            reg.add_or_merge(a_device(0));

        check_eq(reg.size(), 4, "four unidentified devices are four devices");
        check_eq(reg.unidentified_count(), 4, "and all four are counted as such");
        check(!reg.devices()[0].has_stable_identity(), "none has a stable identity");
    }

    void test_merge_keeps_what_the_new_view_did_not_see()
    {
        section("merging does not blank fields the rescan did not read");

        DeviceRecord full = a_device(5001, ProductClassId::Cm5);
        full.firmware     = 530;
        full.mini_type    = 1;
        full.address_note = "192.168.1.50";

        Registry reg;
        reg.add_or_merge(full);

        // A serial-line scan that learns only the serial must not erase the
        // firmware and address learned over IP.
        DeviceRecord sparse;
        sparse.serial_number = 5001;
        sparse.provenance    = Provenance::SerialScan;
        reg.add_or_merge(sparse);

        const auto& d = reg.devices()[0];
        check_eq(d.firmware, 530, "firmware survived");
        check_eq(d.mini_type, 1, "mini_type survived");
        check(d.address_note == "192.168.1.50", "address survived");
        check(d.product == ProductClassId::Cm5, "product survived");
    }

    void test_merge_keeps_reachability_the_rescan_did_not_read()
    {
        section("merging does not blank connection fields either");

        // The same property as the test above, for the fields that test was
        // named after but never touched. The connection merge arrived later
        // and went in as a WHOLESALE REPLACE, under a comment promising
        // conservative merging - so this is the third bug in this codebase to
        // sit in the gap between a test's name and what it exercises.
        //
        // It bites because to_record leaves modbus_slave_id at 0 when a device
        // reports no id, and host is always set on a scan record: the replace
        // always fired, and an id learned on an earlier scan was overwritten
        // with one the device never reported.
        DeviceRecord first = a_device(5101);
        first.connection.transport       = Transport::BacnetIp;
        first.connection.host            = "192.168.1.50";
        first.connection.modbus_slave_id = 5;
        first.connection.device_instance = 4001;

        Registry reg;
        reg.add_or_merge(first);

        // The same device on a new address, reporting no Modbus id this time.
        DeviceRecord moved = a_device(5101);
        moved.connection.transport       = Transport::BacnetIp;
        moved.connection.host            = "192.168.1.77";
        moved.connection.modbus_slave_id = 0;   // did not say
        moved.connection.device_instance = 0;   // did not say
        reg.add_or_merge(moved);

        const auto& c = reg.devices()[0].connection;
        check(c.host == "192.168.1.77", "the new address is taken");
        check_eq(c.modbus_slave_id, 5, "the id it did NOT report this time survived");
        check_eq(c.device_instance, 4001, "and so did the BACnet instance");
    }

    void test_a_scanned_device_is_not_given_an_id_it_never_reported()
    {
        section("a device that reported no Modbus id is not recorded on id 1");

        // Connection::modbus_slave_id defaults to 1. A scan record built for a
        // device that reported nothing therefore used to claim id 1 - a value
        // nobody observed, indistinguishable from a device genuinely there.
        // Checked here as well as in the scanner suite because this is the
        // record the rest of the tool reads.
        DeviceRecord d;
        d.serial_number = 5201;
        check_eq(d.connection.modbus_slave_id, 1,
                 "the struct default really is 1 - this is why it mattered");
    }

    void test_reached_is_sticky_but_provenance_upgrades()
    {
        section("reached only goes true; provenance upgrades toward evidence");

        DeviceRecord typed;
        typed.serial_number = 6001;
        typed.provenance    = Provenance::ManuallyAdded;
        typed.reached       = false;

        Registry reg;
        reg.add_or_merge(typed);
        check(!reg.devices()[0].reached, "a typed-in device has not been reached");

        // It then answers a broadcast: now it demonstrably exists.
        DeviceRecord answered = a_device(6001);
        reg.add_or_merge(answered);

        check(reg.devices()[0].reached, "now reached");
        check(reg.devices()[0].provenance == Provenance::BacnetBroadcast,
              "provenance upgraded from hand-entry to evidence");

        // A later failed contact does not unmake the fact that it answered.
        DeviceRecord quiet;
        quiet.serial_number = 6001;
        quiet.reached       = false;
        reg.add_or_merge(quiet);
        check(reg.devices()[0].reached, "still reached");
    }

    void test_selection_clears_rather_than_clamps()
    {
        section("a stale selection clears instead of pointing somewhere else");

        Registry reg;
        reg.add_or_merge(a_device(7001));
        reg.add_or_merge(a_device(7002));

        reg.select(1);
        if (require(reg.selected() != nullptr, "selected"))
            check_eq(reg.selected()->serial_number, 7002, "the right one");

        // The dangerous version of this clamps to the last device, so a page
        // that was open across a rescan silently starts showing - and
        // writing to - somebody else's controller.
        reg.select(9);
        check(reg.selected() == nullptr, "an out-of-range selection selects nothing");
        check_eq(reg.selected_index(), -1, "and reports no index");

        reg.select(0);
        check(reg.selected() != nullptr, "selectable again");
        reg.clear();
        check(reg.selected() == nullptr, "clearing the registry clears the selection");
    }

    void test_a_selection_never_slides_onto_another_device()
    {
        section("a selection follows its device or clears - it never slides");

        // The half the out-of-range test above does not cover, and the half
        // that actually happens.
        //
        // Devices answer a broadcast in whatever order they answer, so the
        // same subnet scanned twice produces the same controllers at
        // different positions. An index survives that intact and means
        // something different afterwards. Nothing is ever out of range, so a
        // bounds check sees no problem at all.
        Registry reg;
        reg.add_or_merge(a_device(8001));
        reg.add_or_merge(a_device(8002));
        reg.add_or_merge(a_device(8003));

        const Handle picked = handle_at(reg, 1);
        if (require(reg.select_by_handle(picked), "8002 selected"))
        {
            check_eq(reg.selected()->serial_number, 8002u, "the right one");
            check_eq(reg.selected_index(), 1, "at index 1");
        }

        // The operator clears the list and scans again. The same three
        // devices answer, in reverse.
        reg.clear();
        check(reg.selected() == nullptr, "the clear dropped the selection");

        reg.add_or_merge(a_device(8003));
        reg.add_or_merge(a_device(8002));
        reg.add_or_merge(a_device(8001));

        // This is the request a page renders before the rescan and sends
        // after it. Index 1 is in range in both lists and holds 8002 in the
        // first and 8002 in the second only by luck; the handle is the only
        // key that is either right or absent.
        check(!reg.select_by_handle(picked),
              "the pre-rescan handle no longer resolves");
        check(reg.selected() == nullptr, "so nothing is selected");
        check_eq(reg.selected_index(), -1, "and no index is reported");

        // Handles are not reissued, so the old one cannot come back meaning
        // a different device.
        for (const auto& d : reg.devices())
            check(d.handle != picked, "no device reuses the retired handle");

        // And a failed selection must drop whatever was selected BEFORE it,
        // not leave it standing. Checked from a live selection, because the
        // assertions above run from an already-empty one and so pass whether
        // or not the clearing happens at all - a mutation removing the clear
        // survived this test until this block was added.
        const Handle live = handle_at(reg, 0);
        check(reg.select_by_handle(live), "a real device is selected");
        check(reg.selected() != nullptr, "and it took");

        check(!reg.select_by_handle(to_handle(123456)), "then a stale key arrives");
        check(reg.selected() == nullptr,
              "which clears the selection rather than leaving the old one");
        check_eq(reg.selected_index(), -1, "and reports no index");
    }

    void test_a_selection_holds_its_device_across_a_merge()
    {
        section("a selection holds when its device is merged, not replaced");

        // The other direction. A rescan that merges rather than rebuilds must
        // NOT drop a selection - the device is still there, and clearing it
        // would make the tool unusable on any site where a scan is re-run.
        Registry reg;
        reg.add_or_merge(a_device(8001));
        reg.add_or_merge(a_device(8002));

        const Handle picked = handle_at(reg, 1);
        check(reg.select_by_handle(picked), "8002 selected");

        // 8002 answers again, with a newly learned address, alongside a
        // device that was not there before.
        DeviceRecord again = a_device(8002);
        again.address_note = "192.168.1.77";
        reg.add_or_merge(again);

        reg.add_or_merge(a_device(8004));

        check_eq(reg.size(), 3, "one new device, one merged");
        if (require(reg.selected() != nullptr, "still selected"))
        {
            check_eq(reg.selected()->serial_number, 8002u, "still 8002");
            check(reg.selected()->address_note == "192.168.1.77",
                  "and it picked up what the rescan learned");
        }

        // A merge must not mint a second handle for a device already here.
        check_eq((int)to_number(reg.devices()[1].handle), (int)to_number(picked),
                 "the merged device kept its handle");
    }

    void test_unidentified_devices_are_still_selectable()
    {
        section("a device with no serial can still be opened");

        // Handles exist partly for this. A serial number would be the
        // obvious key, and it is missing on exactly the devices most worth
        // looking at - the ones this tool flags for repair. Keying on serial
        // would make the broken ones unreachable.
        Registry reg;
        reg.add_or_merge(a_device(0));
        reg.add_or_merge(a_device(0));
        check_eq(reg.size(), 2, "two unidentified devices, not merged into one");

        const Handle first  = handle_at(reg, 0);
        const Handle second = handle_at(reg, 1);
        check(first != second, "and they have different handles");

        if (require(reg.select_by_handle(second), "the second one is selectable"))
        {
            check_eq(reg.selected_index(), 1, "and it is the second one");
            check(!reg.selected()->has_stable_identity(), "still has no identity");
        }
    }

    // ---------------------------------------------------------------------
    // Duplicate Modbus ids. These used to live in the scanner suite and ran
    // over one scan's results; the third and fourth cases below are the ones
    // that arrangement could not express at all.

    DeviceRecord a_device_on_modbus_id(uint32_t serial, int modbus_id)
    {
        DeviceRecord d = a_device(serial);
        d.modbus_id_reported = modbus_id;
        d.connection.host = "192.168.1.60";
        d.observation_complete = true;
        return d;
    }

    void test_a_duplicate_id_is_flagged_on_every_participant()
    {
        section("a duplicate Modbus id is flagged on each device involved");

        Registry reg;
        reg.add_or_merge(a_device_on_modbus_id(1001, 5));
        reg.add_or_merge(a_device_on_modbus_id(1002, 5));
        reg.add_or_merge(a_device_on_modbus_id(1003, 7));

        check_eq(reg.refresh_duplicate_modbus_ids(), 2, "two are in conflict");

        // A duplicate is a property of a pair, so BOTH must be flagged -
        // picking one to blame would be arbitrary, and the operator has to
        // see which two are fighting.
        check(reg.devices()[0].needs_attention(), "the first is flagged");
        check(reg.devices()[1].needs_attention(), "and so is the second");
        check(!reg.devices()[2].needs_attention(), "the one on its own is not");
    }

    void test_id_zero_is_not_a_conflict()
    {
        section("several devices reporting Modbus id 0 are not in conflict");

        // 0 is not an address. Treating it as one would flag every
        // unconfigured device on a subnet as conflicting with every other.
        Registry reg;
        reg.add_or_merge(a_device_on_modbus_id(2001, 0));
        reg.add_or_merge(a_device_on_modbus_id(2002, 0));
        reg.add_or_merge(a_device_on_modbus_id(2003, 0));

        check_eq(reg.refresh_duplicate_modbus_ids(), 0, "none flagged");
    }

    void test_a_duplicate_across_two_scans_is_still_found()
    {
        section("two devices on one id are found even on separate scans");

        // The case per-scan detection could not see at all. Each scan holds
        // one device, so neither scan contains a conflict - but the registry
        // holds both, and the conflict is real.
        Registry reg;
        reg.add_or_merge(a_device_on_modbus_id(3001, 5));
        check_eq(reg.refresh_duplicate_modbus_ids(), 0, "one device, no conflict yet");

        reg.add_or_merge(a_device_on_modbus_id(3002, 5));
        check_eq(reg.refresh_duplicate_modbus_ids(), 2,
                 "the second scan reveals the conflict with the first");
    }

    void test_a_resolved_duplicate_stops_being_reported()
    {
        section("a duplicate that is no longer true is withdrawn from both");

        // The other case per-scan detection got wrong, and the worse one.
        // Both devices were flagged; then one is renumbered. If the stale
        // repair survived, one device would go on saying "id 5 is claimed by
        // 2 devices" while the page showed the other one as clean.
        Registry reg;
        reg.add_or_merge(a_device_on_modbus_id(4001, 5));
        reg.add_or_merge(a_device_on_modbus_id(4002, 5));
        check_eq(reg.refresh_duplicate_modbus_ids(), 2, "both flagged");

        reg.add_or_merge(a_device_on_modbus_id(4002, 6));

        check_eq(reg.refresh_duplicate_modbus_ids(), 0, "nobody is in conflict now");
        check(!reg.devices()[0].needs_attention(), "the first is clean");
        check(!reg.devices()[1].needs_attention(), "and so is the one that moved");
        check_eq((int)reg.pending_repairs().size(), 0, "nothing left pending");
    }

    void test_refreshing_does_not_stack_repeats()
    {
        section("refreshing twice does not report the same conflict twice");

        Registry reg;
        reg.add_or_merge(a_device_on_modbus_id(5001, 5));
        reg.add_or_merge(a_device_on_modbus_id(5002, 5));

        reg.refresh_duplicate_modbus_ids();
        reg.refresh_duplicate_modbus_ids();
        reg.refresh_duplicate_modbus_ids();

        check_eq((int)reg.devices()[0].repairs.size(), 1, "one repair, not three");
        check_eq((int)reg.pending_repairs().size(), 2, "two pending in total");
    }

    void test_refreshing_leaves_other_repairs_alone()
    {
        section("refreshing duplicates does not disturb a serial repair");

        // The clear-then-rederive step must remove duplicate repairs only. A
        // device with no serial has a different and more serious problem, and
        // losing it here would be a silent downgrade.
        Registry reg;
        DeviceRecord nameless = a_device_on_modbus_id(0, 5);
        nameless.repairs.push_back(a_repair(RepairKind::AssignSerialNumber));
        reg.add_or_merge(nameless);
        reg.add_or_merge(a_device_on_modbus_id(6002, 5));

        reg.refresh_duplicate_modbus_ids();
        reg.refresh_duplicate_modbus_ids();

        const auto& repairs = reg.devices()[0].repairs;
        check_eq((int)repairs.size(), 2, "the serial repair plus one duplicate repair");

        bool has_serial = false;
        for (const auto& r : repairs)
            if (r.kind == RepairKind::AssignSerialNumber) has_serial = true;
        check(has_serial, "the serial repair survived");
    }

    void test_uninitialised_serial_detection()
    {
        section("both uninitialised serial values are detected");

        check(is_uninitialised_serial(0u), "0 is uninitialised");
        check(is_uninitialised_serial(0xFFFFFFFFu), "0xFFFFFFFF is uninitialised");

        // T3000 tests the second case as 255*255*255*255, which is
        // 4,228,250,625 - NOT 0xFFFFFFFF (4,294,967,295). Asserting the two
        // differ documents why this function exists rather than reusing the
        // original expression, and would fail loudly if anyone "simplified"
        // it back.
        check(255u * 255u * 255u * 255u != 0xFFFFFFFFu,
              "T3000's constant is not the all-bits-set value it reaches for");
        check(!is_uninitialised_serial(255u * 255u * 255u * 255u),
              "and that constant is just an ordinary serial, correctly ignored");

        // Real serials, including the range T3000 randomly assigns.
        check(!is_uninitialised_serial(500123u), "a real serial is not flagged");
        check(!is_uninitialised_serial(200000u), "nor the bottom of the assigned range");
        check(!is_uninitialised_serial(299999u), "nor the top");
    }

    void test_all_ff_devices_never_merge()
    {
        section("devices reporting an all-FF serial stay separate too");

        // Regression. serial_number was an int, so 0xFFFFFFFF arrived as -1,
        // and -1 != 0 meant has_stable_identity() called it a real identity.
        // Two unidentified devices then merged into one record - the exact
        // collapse unidentified_count() exists to prevent, and a technician
        // would have been told there was one nameless device when there were
        // two.
        Registry reg;
        reg.add_or_merge(a_device(0xFFFFFFFFu));
        reg.add_or_merge(a_device(0xFFFFFFFFu));
        reg.add_or_merge(a_device(0xFFFFFFFFu));

        check_eq(reg.size(), 3, "three all-FF devices are three devices");
        check_eq(reg.unidentified_count(), 3, "and all three are counted");
        check(!reg.devices()[0].has_stable_identity(), "none has a stable identity");

        // Mixed with plain zeros, still all separate.
        reg.add_or_merge(a_device(0));
        check_eq(reg.size(), 4, "a zero-serial device is a fourth device");
        check_eq(reg.unidentified_count(), 4, "and is also unidentified");

        // A real serial still merges normally.
        reg.add_or_merge(a_device(9001));
        reg.add_or_merge(a_device(9001));
        check_eq(reg.size(), 5, "two sightings of a real device are one device");
    }

    void test_a_clean_rescan_withdraws_a_stale_approval()
    {
        section("a rescan that finds nothing wrong clears an approved repair");

        // Regression. Clearing approvals used to sit inside the branch that
        // replaced the repair list, so a rescan finding NO problems left the
        // old repair in place AND still approved - approved, and describing a
        // device state that no longer existed.
        DeviceRecord broken = a_device(7777);
        broken.observation_complete = true;
        broken.repairs.push_back(a_repair());

        Registry reg;
        const int i = reg.add_or_merge(broken);
        check(reg.approve_repair(handle_at(reg, i), 0), "approved while the problem existed");
        check_eq((int)reg.pending_repairs().size(), 0, "nothing pending");

        // The problem is fixed elsewhere; the next scan sees a healthy device.
        DeviceRecord healthy = a_device(7777);
        healthy.observation_complete = true;   // a full look, finding nothing
        reg.add_or_merge(healthy);

        check_eq(reg.size(), 1, "still one device");
        check_eq((int)reg.devices()[0].repairs.size(), 0, "the stale repair is gone");
        check(!reg.devices()[0].needs_attention(), "and the device is clean");
    }

    void test_a_partial_merge_does_not_erase_known_problems()
    {
        section("a partial sighting does not clear repairs it never looked for");

        // The other side of the same rule. A serial sweep that learns only an
        // address has not looked for problems, so it must not appear to have
        // found none.
        DeviceRecord scanned = a_device(8888);
        scanned.observation_complete = true;
        scanned.repairs.push_back(a_repair());

        Registry reg;
        reg.add_or_merge(scanned);
        check_eq((int)reg.devices()[0].repairs.size(), 1, "a repair is known");

        DeviceRecord glimpse;
        glimpse.serial_number = 8888;
        glimpse.provenance    = Provenance::SerialScan;
        glimpse.observation_complete = false;
        reg.add_or_merge(glimpse);

        check_eq((int)reg.devices()[0].repairs.size(), 1, "the repair survives");
        check(!reg.devices()[0].repairs[0].approved,
              "but its approval does not - a merge always withdraws consent");
    }

    void test_labels_exist_for_everything_shown()
    {
        section("provenance and repair kinds have labels");

        const Provenance ps[] = {
            Provenance::ManuallyAdded, Provenance::BacnetBroadcast,
            Provenance::BacnetUnicast, Provenance::SerialScan, Provenance::Restored,
        };
        for (auto p : ps)
            check(to_string(p)[0] != '\0', to_string(p));

        const RepairKind ks[] = {
            RepairKind::AssignSerialNumber,
            RepairKind::ResolveDuplicateModbusId,
            RepairKind::ConfigureGatewaySubPort,
        };
        for (auto k : ks)
            check(to_string(k)[0] != '\0', to_string(k));
    }
}

namespace
{
    void test_removing_clears_the_selection_and_never_reuses_a_handle()
    {
        section("removing a device clears it from the selection, and its handle is not reused");

        Registry reg;
        reg.add_or_merge(a_device(8001));
        reg.add_or_merge(a_device(8002));
        const Handle gone = handle_at(reg, 0);
        const Handle kept = handle_at(reg, 1);

        reg.select_by_handle(gone);
        check(reg.remove(gone), "the device is removed");
        check_eq(reg.size(), 1, "one is left");
        check(reg.selected() == nullptr, "nothing is selected, rather than the next device");
        check(reg.selected_handle() == kNoHandle, "and the page is not sent the removed device's handle");
        check(!reg.remove(gone), "removing it again finds nothing");

        reg.select_by_handle(kept);
        check(reg.remove(handle_at(reg, 0)) && reg.selected_handle() == kNoHandle,
              "removing the selected one clears the selection");

        reg.add_or_merge(a_device(8003));
        check(handle_at(reg, 0) != gone && handle_at(reg, 0) != kept,
              "a new device gets a handle no removed device had");
    }

    void test_a_merge_never_takes_a_placement()
    {
        section("a name and location are the operator's, and a scan never changes them");

        Registry reg;
        reg.add_or_merge(a_device(8001));

        Placement p;
        p.name = "Boiler";
        p.room = "Plant";
        check(reg.set_placement(handle_at(reg, 0), p), "the device is named");
        check(!reg.set_placement(to_handle(999), p), "a handle not in the list is refused");

        DeviceRecord rescan = a_device(8001);
        rescan.placement.name = "something a record happened to carry";
        reg.add_or_merge(rescan);
        check(reg.devices()[0].placement.name == "Boiler", "the name survives a merge");
        check(reg.devices()[0].placement.room == "Plant", "and so does the room");

        DeviceRecord blank = a_device(8001);
        reg.add_or_merge(blank);
        check(reg.devices()[0].placement.name == "Boiler", "a record with no name does not blank it");
    }

    void test_history_merges_forwards()
    {
        section("when a device was seen only moves forwards, and its first sighting only back");

        Registry reg;
        DeviceRecord d = a_device(8001);
        d.first_seen    = 100;
        d.last_seen     = 100;
        d.answered_scan = 1;
        d.panel_name    = "AHU";
        reg.add_or_merge(d);

        DeviceRecord later = a_device(8001);
        later.first_seen    = 300;
        later.last_seen     = 300;
        later.answered_scan = 2;
        reg.add_or_merge(later);
        check_eq((long)reg.devices()[0].first_seen, 100, "the first sighting stays the earliest");
        check_eq((long)reg.devices()[0].last_seen, 300, "the last sighting moves on");
        check_eq(reg.devices()[0].answered_scan, 2, "as does the scan it answered");
        check(reg.devices()[0].panel_name == "AHU", "a record with no panel name keeps the old one");

        DeviceRecord stale = a_device(8001);
        stale.last_seen     = 200;
        stale.answered_scan = 1;
        stale.panel_name    = "AHU 2";
        reg.add_or_merge(stale);
        check_eq((long)reg.devices()[0].last_seen, 300, "an older sighting does not move it back");
        check_eq(reg.devices()[0].answered_scan, 2, "nor an older scan");
        check(reg.devices()[0].panel_name == "AHU 2", "a new panel name is taken");
    }

    void test_answered_last_scan_follows_the_count()
    {
        section("answered the last scan means the most recent one, and nothing before any scan");

        Registry reg;
        DeviceRecord d = a_device(8001);
        reg.add_or_merge(d);
        check(!reg.answered_last_scan(reg.devices()[0]), "no scan yet, so nothing answered one");

        const int first = reg.begin_scan();
        check_eq(first, 1, "scans are numbered from 1");
        d.answered_scan = first;
        reg.add_or_merge(d);
        check(reg.answered_last_scan(reg.devices()[0]), "it answered scan 1");

        reg.begin_scan();
        check(!reg.answered_last_scan(reg.devices()[0]), "and not scan 2");

        reg.clear();
        check_eq(reg.begin_scan(), 3, "numbers are not reused after a clear");
    }

    void test_a_restored_device_takes_no_part_in_duplicates()
    {
        section("a device known only from the saved list is not counted as a duplicate");

        Registry reg;
        DeviceRecord live = a_device(8001);
        live.modbus_id_reported = 5;
        DeviceRecord restored = a_device(8002);
        restored.modbus_id_reported = 5;
        restored.provenance = Provenance::Restored;
        reg.add_or_merge(live);
        reg.add_or_merge(restored);

        check_eq(reg.refresh_duplicate_modbus_ids(), 0, "one live and one restored on id 5 is no conflict");
        check(reg.pending_repairs().empty(), "and neither carries a repair");

        DeviceRecord answers = a_device(8002);
        answers.modbus_id_reported = 5;
        reg.add_or_merge(answers);
        check_eq(reg.refresh_duplicate_modbus_ids(), 2, "once the restored one answers, both are flagged");
    }
}

int run_registry_tests()
{
    test_repairs_start_unapproved();
    test_approval_is_per_repair_and_per_device();
    test_approval_does_not_survive_a_rescan();
    test_stale_keys_are_refused_not_clamped();
    test_merge_only_on_serial();
    test_unidentified_devices_never_merge();
    test_merge_keeps_what_the_new_view_did_not_see();
    test_merge_keeps_reachability_the_rescan_did_not_read();
    test_a_scanned_device_is_not_given_an_id_it_never_reported();
    test_reached_is_sticky_but_provenance_upgrades();
    test_selection_clears_rather_than_clamps();
    test_a_selection_never_slides_onto_another_device();
    test_a_selection_holds_its_device_across_a_merge();
    test_unidentified_devices_are_still_selectable();
    test_all_ff_devices_never_merge();
    test_a_clean_rescan_withdraws_a_stale_approval();
    test_a_partial_merge_does_not_erase_known_problems();
    test_a_duplicate_id_is_flagged_on_every_participant();
    test_id_zero_is_not_a_conflict();
    test_a_duplicate_across_two_scans_is_still_found();
    test_a_resolved_duplicate_stops_being_reported();
    test_refreshing_does_not_stack_repeats();
    test_refreshing_leaves_other_repairs_alone();
    test_uninitialised_serial_detection();
    test_labels_exist_for_everything_shown();
    test_removing_clears_the_selection_and_never_reuses_a_handle();
    test_a_merge_never_takes_a_placement();
    test_history_merges_forwards();
    test_answered_last_scan_follows_the_count();
    test_a_restored_device_takes_no_part_in_duplicates();
    return 0;
}
