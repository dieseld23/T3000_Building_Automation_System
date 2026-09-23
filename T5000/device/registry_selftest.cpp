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

int run_registry_tests()
{
    test_repairs_start_unapproved();
    test_approval_is_per_repair_and_per_device();
    test_approval_does_not_survive_a_rescan();
    test_stale_keys_are_refused_not_clamped();
    test_merge_only_on_serial();
    test_unidentified_devices_never_merge();
    test_merge_keeps_what_the_new_view_did_not_see();
    test_reached_is_sticky_but_provenance_upgrades();
    test_selection_clears_rather_than_clamps();
    test_a_selection_never_slides_onto_another_device();
    test_a_selection_holds_its_device_across_a_merge();
    test_unidentified_devices_are_still_selectable();
    test_all_ff_devices_never_merge();
    test_a_clean_rescan_withdraws_a_stale_approval();
    test_a_partial_merge_does_not_erase_known_problems();
    test_uninitialised_serial_detection();
    test_labels_exist_for_everything_shown();
    return 0;
}
