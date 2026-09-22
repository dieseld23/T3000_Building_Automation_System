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

    DeviceRecord a_device(int serial, ProductClassId product = ProductClassId::Tstat10)
    {
        DeviceRecord d;
        d.serial_number = serial;
        d.product       = product;
        d.provenance    = Provenance::BacnetBroadcast;
        d.reached       = true;
        return d;
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

        check(reg.approve_repair(ia, 0), "approve the first repair on the first device");

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
        check(reg.approve_repair(i, 0), "approved");
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

    void test_stale_indices_are_refused_not_clamped()
    {
        section("out-of-range approvals are refused");

        // A page held open across a rescan will send indices that no longer
        // mean what they did. Clamping would approve a write on whichever
        // device happens to be last.
        Registry reg;
        reg.add_or_merge(a_device(3001));

        check(!reg.approve_repair(0, 0), "no such repair");
        check(!reg.approve_repair(5, 0), "no such device");
        check(!reg.approve_repair(-1, 0), "negative device index");
        check(!reg.approve_repair(0, -1), "negative repair index");
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
        check(reg.selected() != nullptr, "selected");
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
    test_stale_indices_are_refused_not_clamped();
    test_merge_only_on_serial();
    test_unidentified_devices_never_merge();
    test_merge_keeps_what_the_new_view_did_not_see();
    test_reached_is_sticky_but_provenance_upgrades();
    test_selection_clears_rather_than_clamps();
    test_labels_exist_for_everything_shown();
    return 0;
}
