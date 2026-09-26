// Tests for configuring a device's inputs offline: which devices can be, the
// payload the Inputs page gets for one, and changes made and undone against a
// saved list in memory.
//
// The property most worth guarding is where the configuration lives. It is
// the device entry's, in the file: it survives a restart and a scan that
// finds the device, and goes when, and only when, the device is forgotten.

#include "offline_inputs.h"

#include "device_list.h"
#include "../discovery/scanner.h"
#include "../testing/check.h"

namespace
{
    using namespace t5000::app;
    using namespace t5000::device;
    using namespace t5000::testing;
    namespace store   = t5000::store;
    namespace offline = t5000::offline;

    bool contains(const std::string& text, const std::string& part)
    {
        return text.find(part) != std::string::npos;
    }

    DeviceRecord typed_in(uint32_t serial, ProductClassId product, int mini_type)
    {
        DeviceRecord d;
        d.serial_number = serial;
        d.product       = product;
        d.mini_type     = mini_type;
        d.provenance    = Provenance::ManuallyAdded;
        return d;
    }

    // A saved list in memory with one device added by hand, as the Add
    // dialog adds one.
    struct Bench
    {
        Registry       registry;
        store::DeviceDb db;
        StoreStatus    status;
        Handle         handle = kNoHandle;

        bool open()
        {
            std::string error;
            if (!db.open(":memory:", error))
            {
                printf("  (could not open a database in memory: %s)\n", error.c_str());
                return false;
            }
            status.saving = true;
            status.path   = ":memory:";
            return true;
        }

        bool add(uint32_t serial, ProductClassId product, int mini_type)
        {
            HandAdded device;
            device.serial    = serial;
            device.product   = product;
            device.mini_type = mini_type;
            std::string message;
            const bool ok = add_device(registry, db, device, status, handle, message);
            if (!ok)
                printf("  (could not add %u: %s)\n", serial, message.c_str());
            return ok;
        }

        const DeviceRecord* device() const
        {
            for (const auto& d : registry.devices())
                if (d.handle == handle)
                    return &d;
            return nullptr;
        }

        bool edit(int index, offline::InputField field, const std::string& value, std::string* message_out = nullptr)
        {
            InputEditRequest r;
            r.handle = handle;
            r.index  = index;
            r.field  = field;
            r.value  = value;
            std::string message;
            const bool ok = edit_offline_input(registry, db, status, r, message);
            if (message_out)
                *message_out = message;
            return ok;
        }

        std::vector<store::OfflinePoint> saved(uint32_t serial)
        {
            std::vector<store::OfflinePoint> out;
            std::string error;
            check(db.load_offline_inputs(serial, out, error), "the saved changes load");
            return out;
        }

        std::string payload()
        {
            const DeviceRecord* d = device();
            return d ? offline_inputs_payload(db, status, *d) : std::string();
        }
    };

    void test_which_devices_are_configured_offline()
    {
        section("inputs are configured offline on an entry added by hand, read by private transfer, with a model");

        DeviceRecord found = typed_in(9001, ProductClassId::Tstat10, 11);
        found.provenance = Provenance::BacnetBroadcast;
        check(!is_configured_offline(found), "a device a scan found is read, not configured offline");
        check(!plan_offline_inputs(found).can_edit, "  and its plan says so");

        const OfflineInputsPlan oem = plan_offline_inputs(typed_in(9002, ProductClassId::Tstat10, 11));
        check(oem.can_edit, "a T3-OEM added by hand is configured offline");
        check(oem.model == "T3-OEM", "  as a T3-OEM");
        check_eq(oem.inputs, 64, "  with 64 inputs, as T3000 gives a panel it has not read");
        check_eq(oem.rows, 64, "  all 64 shown");

        const OfflineInputsPlan unknown = plan_offline_inputs(typed_in(9003, ProductClassId::Tstat10, 0));
        check(!unknown.can_edit, "a TSTAT10 whose model is not known is not, yet");
        check(contains(unknown.reason, "Choose its model"), "  and the reason says to choose its model");

        const OfflineInputsPlan cm5 = plan_offline_inputs(typed_in(9004, ProductClassId::Cm5, 0));
        check(cm5.can_edit, "a CM5 is: panel type 0 is the CM5's own");
        check(cm5.model == "CM5", "  and it is shown as a CM5");

        const OfflineInputsPlan bb = plan_offline_inputs(typed_in(9005, ProductClassId::MiniPanelArm, 5));
        check(bb.can_edit && bb.model == "T3-BB", "a T3-BB is");
        const OfflineInputsPlan rmc = plan_offline_inputs(typed_in(9006, ProductClassId::Esp32T3Series, 29));
        check(rmc.can_edit && rmc.model == "T3-RMC-1232", "a T3-RMC-1232 is");

        const OfflineInputsPlan mini = plan_offline_inputs(typed_in(9007, ProductClassId::MiniPanel, 0));
        check(!mini.can_edit, "a MiniPanel is not: T3000 names no model of it to choose");
        check(contains(mini.reason, "names no model"), "  and the reason says so");

        const OfflineInputsPlan tstat = plan_offline_inputs(typed_in(9008, ProductClassId::Tstat8, 0));
        check(!tstat.can_edit, "a TSTAT8 is not: it is not read by private transfer");
        check(contains(tstat.reason, "private transfer"), "  and the reason says so");
    }

    void test_a_new_configuration_is_t3000s()
    {
        section("a device's configuration starts as T3000 starts a panel's inputs");

        Bench b;
        if (!b.open() || !b.add(9101, ProductClassId::Tstat10, 11))
            return;

        const std::string p = b.payload();
        check(contains(p, "{\"offline\":{\"handle\":\"" + std::to_string(to_number(b.handle)) +
                              "\",\"model\":\"T3-OEM\",\"saving\":true,\"edited\":0,"),
              "the payload says it is configured offline, for this device, as a T3-OEM, being saved, with nothing "
              "changed");
        check(contains(p, "\"editable\":[\"fullLabel\",\"autoManual\",\"filter\",\"label\"]"),
              "  and which columns can be changed");
        check(contains(p, "\"isFixture\":false,\"readFromWire\":false"), "  and never that it was read");
        check(contains(p, "\"readPath\":{\"path\":\"none\",\"summary\":\"configured offline\""),
              "  and its read path is none");
        check(contains(p, "\"count\":64,"), "  with 64 inputs");
        check(contains(p, "\"index\":0,\"input\":1,\"fullLabel\":\"IN1\",\"label\":\"\",\"autoManual\":\"Auto\""),
              "the first is IN1, unlabelled, on Auto");
        check(contains(p, "\"filter\":\"5\""), "  with filter 5");
        check(contains(p, "\"fullLabel\":\"IN64\""), "the last is IN64");
        check(contains(p, "\"changed\":[]"), "and nothing is marked changed");
        check(!contains(p, "\"changed\":[\""), "  anywhere");
        check(b.saved(9101).empty(), "nothing is saved for it until something changes");
    }

    void test_a_change_is_saved_with_what_it_started_from()
    {
        section("a change is saved as the input's bytes, with the bytes it started from");

        Bench b;
        if (!b.open() || !b.add(9201, ProductClassId::Tstat10, 11))
            return;

        check(b.edit(2, offline::InputField::Label, "ahu-1"), "input 3 is labelled");
        auto saved = b.saved(9201);
        if (require(saved.size() == 1, "one input is saved"))
        {
            check_eq(saved[0].index, 2, "  input 3");
            const offline::InputBytes start = offline::default_input(2);
            check(saved[0].base == std::vector<uint8_t>(start.begin(), start.end()), "  with IN3 as it started");
            check(std::string((const char*)&saved[0].edited[offline::input_at::label]) == "AHU_1",
                  "  and its label AHU_1");
        }

        const std::string p = b.payload();
        check(contains(p, "\"edited\":1,"), "the payload counts one input changed");
        check(contains(p, "\"label\":\"AHU_1\""), "  shows the label");
        check(contains(p, "\"changed\":[\"label\"]"), "  and marks its Label changed");

        // A second change keeps the base; putting it back removes the row.
        check(b.edit(2, offline::InputField::Filter, "9"), "its filter is changed too");
        check(b.edit(2, offline::InputField::Label, ""), "its label is cleared again");
        saved = b.saved(9201);
        if (require(saved.size() == 1, "  it is still saved, for its filter"))
        {
            check_eq(saved[0].base[offline::input_at::filter], 5, "  with base filter 5, what it started from");
            check_eq(saved[0].edited[offline::input_at::filter], 9, "  and filter 9");
        }
        check(contains(b.payload(), "\"changed\":[\"filter\"]"), "  and only the filter marked changed");

        check(b.edit(2, offline::InputField::Filter, "5"), "the filter is put back by hand");
        check(b.saved(9201).empty(), "and nothing is left saved: it is as it started");
    }

    void test_a_refused_change_saves_nothing()
    {
        section("a refused change saves nothing and says why");

        Bench b;
        if (!b.open() || !b.add(9301, ProductClassId::Tstat10, 11))
            return;

        std::string message;
        check(!b.edit(0, offline::InputField::FullLabel, "IN2", &message), "input 2's name for input 1 is refused");
        check(contains(message, "input 2"), "  and the message says whose it is");
        check(!b.edit(0, offline::InputField::Filter, "300"), "a filter of 300 is refused");
        check(!b.edit(64, offline::InputField::Filter, "1"), "input 65 of 64 is refused");
        check(b.saved(9301).empty(), "and nothing is saved");

        check(b.edit(0, offline::InputField::FullLabel, "IN1"), "input 1 given its own name is taken");
        check(b.saved(9301).empty(), "  and saves nothing, since nothing changed");
    }

    void test_changes_are_refused_where_they_would_not_be_kept_or_shown()
    {
        section("changes are refused where they could not be kept or seen");

        Bench b;
        if (!b.open() || !b.add(9401, ProductClassId::Tstat10, 0))
            return;

        std::string message;
        check(!b.edit(0, offline::InputField::Filter, "1", &message), "a TSTAT10 whose model is not known is refused");
        check(contains(message, "Choose its model"), "  saying to choose one");

        Bench gone;
        if (!gone.open())
            return;
        gone.handle = to_handle(77);
        check(!gone.edit(0, offline::InputField::Filter, "1", &message), "a device not in the list is refused");
        check(contains(message, "no longer in the list"), "  saying so");

        // Found by a scan: it is read now, and these are not its inputs.
        Bench found;
        if (!found.open() || !found.add(9402, ProductClassId::Tstat10, 11))
            return;
        DeviceRecord scanned = *found.device();
        scanned.provenance = Provenance::BacnetBroadcast;
        scanned.reached    = true;
        found.registry.add_or_merge(scanned);
        check(!found.edit(0, offline::InputField::Filter, "1", &message), "a device a scan has found is refused");

        // Not being saved: the change would be lost at close.
        Registry registry;
        store::DeviceDb closed;
        StoreStatus status;
        status.error = "it could not be opened";
        DeviceRecord d = typed_in(9403, ProductClassId::Tstat10, 11);
        registry.add_or_merge(d);
        InputEditRequest r;
        r.handle = registry.devices()[0].handle;
        r.index  = 0;
        r.field  = offline::InputField::Filter;
        r.value  = "1";
        check(!edit_offline_input(registry, closed, status, r, message), "a change when the list is not saved is refused");
        check(contains(message, "not being saved") && contains(message, "could not be opened"),
              "  saying why, with the reason the list is not saved");
        check(contains(offline_inputs_payload(closed, status, registry.devices()[0]), "\"saving\":false"),
              "and the payload says it is not being saved");
    }

    void test_undo_puts_an_input_back()
    {
        section("undo puts one input back as it started");

        Bench b;
        if (!b.open() || !b.add(9501, ProductClassId::Cm5, 0))
            return;

        check(b.edit(0, offline::InputField::Label, "a"), "input 1 is changed");
        check(b.edit(1, offline::InputField::Label, "b"), "input 2 is changed");

        std::string message;
        check(revert_offline_input(b.registry, b.db, b.status, b.handle, 0, message), "input 1 is put back");
        const auto saved = b.saved(9501);
        check(saved.size() == 1 && saved[0].index == 1, "  and only input 2's change is left");
        check(revert_offline_input(b.registry, b.db, b.status, b.handle, 0, message),
              "putting back an input with no changes is not an error");
    }

    void test_the_configuration_outlives_a_restart_and_a_scan()
    {
        section("the configuration stays with the entry through a restart and a scan that finds it");

        Bench b;
        if (!b.open() || !b.add(9601, ProductClassId::Tstat10, 11))
            return;
        check(b.edit(4, offline::InputField::FullLabel, "Supply air"), "input 5 is named");

        // A scan finds it: the same row, updated (DeviceDb::write).
        DeviceRecord found = *b.device();
        found.provenance    = Provenance::BacnetBroadcast;
        found.reached       = true;
        found.answered_scan = 1;
        found.last_seen     = 5000;
        found.firmware      = 605;
        b.registry.add_or_merge(found);
        std::string error;
        check(b.db.save_scanned({ *b.device() }, error), "the scan is saved");

        check(b.saved(9601).size() == 1, "the change is still saved");
        check(!is_configured_offline(*b.device()), "the device is read now, not configured offline");

        const std::string note = pending_offline_note(b.db, *b.device());
        check(contains(note, "input 5") && contains(note, "not written"),
              "and its Inputs page will say input 5 was changed offline and is not written");

        std::vector<DeviceRecord> restored;
        check(b.db.load(restored, error) && restored.size() == 1, "a restart restores it");
        check(!restored.empty() && restored[0].provenance == Provenance::Restored, "  as a device that has answered");
        check(b.saved(9601).size() == 1, "  and its change is still there");

        DeviceRecord other = typed_in(9602, ProductClassId::Tstat10, 11);
        check(pending_offline_note(b.db, other).empty(), "a device with no changes has no note");
    }

    void test_forgetting_a_device_takes_its_configuration()
    {
        section("forgetting a device takes its configuration, and a device added after does not inherit it");

        Bench b;
        if (!b.open() || !b.add(9701, ProductClassId::Tstat10, 11))
            return;
        check(b.edit(0, offline::InputField::Label, "gone"), "input 1 is changed");

        ScanSummary summary;
        std::string message;
        check(forget_device(b.registry, b.db, b.handle, summary, message), "the device is forgotten");

        // SQLite gives a new row the highest id plus one, which is the id of
        // the row just deleted when it was the last.
        check(b.add(9702, ProductClassId::Tstat10, 11), "another device is added");
        check(b.saved(9702).empty(), "  and has no changes of the forgotten one's");
        check(b.saved(9701).empty(), "nothing is saved under the forgotten serial");

        check(b.add(9701, ProductClassId::Tstat10, 11), "the forgotten serial added again");
        check(b.saved(9701).empty(), "  starts with nothing changed");

        check(b.edit(0, offline::InputField::Label, "x"), "a change is made to it");
        check(forget_all(b.registry, b.db, message), "every device is forgotten");
        check(b.saved(9701).empty() && b.saved(9702).empty(), "  with every change");
    }

    void test_the_model_can_be_chosen_after_adding()
    {
        section("the model of an entry added by hand can be chosen, or changed, after adding it");

        Bench b;
        if (!b.open() || !b.add(9801, ProductClassId::Tstat10, 0))
            return;

        std::string message;
        Placement p;
        p.name = "Lobby";
        check(place_device(b.registry, b.db, b.handle, p, b.status, message, 11), "T3-OEM is chosen");
        check_eq(b.device()->mini_type, 11, "  and the entry has it");
        check(b.device()->placement.name == "Lobby", "  with the name given with it");

        std::vector<DeviceRecord> restored;
        std::string error;
        check(b.db.load(restored, error) && !restored.empty() && restored[0].mini_type == 11, "  saved in the file");
        check(plan_offline_inputs(*b.device()).can_edit, "its inputs can now be configured");

        check(place_device(b.registry, b.db, b.handle, p, b.status, message, kKeepModel), "a save without a model");
        check_eq(b.device()->mini_type, 11, "  keeps it");
        check(place_device(b.registry, b.db, b.handle, p, b.status, message, 0), "\"model not known\" is taken");
        check_eq(b.device()->mini_type, 0, "  as 0");

        check(!place_device(b.registry, b.db, b.handle, p, b.status, message, 5), "a model of another product is refused");
        check_eq(b.device()->mini_type, 0, "  and the entry keeps its own");

        // A device a scan has found reports what it is.
        Bench f;
        if (!f.open() || !f.add(9802, ProductClassId::Tstat10, 11))
            return;
        DeviceRecord scanned = *f.device();
        scanned.provenance = Provenance::BacnetBroadcast;
        scanned.reached    = true;
        scanned.mini_type  = 0;
        f.registry.add_or_merge(scanned);
        check(!place_device(f.registry, f.db, f.handle, p, f.status, message, 11),
              "a model for a device a scan has found is refused");
        check(contains(message, "not chosen"), "  saying its model is not chosen");
        check(place_device(f.registry, f.db, f.handle, p, f.status, message, kKeepModel),
              "  while its name can still be saved");
    }

    void test_a_model_change_keeps_changes_in_view()
    {
        section("a model with fewer inputs is refused while an input past them has changes");

        // No model T3000 names has fewer than 64 inputs, so the saved list
        // is given a change past 64 directly, as a model with fewer would
        // leave one.
        Bench b;
        if (!b.open() || !b.add(9901, ProductClassId::Tstat10, 11))
            return;

        store::OfflinePoint far;
        far.index = 70;
        const offline::InputBytes start = offline::default_input(70);
        far.base.assign(start.begin(), start.end());
        far.edited = far.base;
        far.edited[offline::input_at::filter] = 1;
        std::string error;
        check(b.db.save_offline_input(9901, far, error), "a change to input 71 is saved");

        std::string message;
        Placement p;
        check(!place_device(b.registry, b.db, b.handle, p, b.status, message, 14),
              "changing to a T3-OEM-12I, of 64 inputs, is refused");
        check(contains(message, "input 71") && contains(message, "Undo"), "  naming the input, and what to do");
        check_eq(b.device()->mini_type, 11, "  and the model stays");

        const std::string payload = b.payload();
        check(contains(payload, "1 input changed earlier is past this model's 64. The changes are kept"),
              "the Inputs page says a saved change is past the model's inputs");
    }

    void test_a_device_without_a_model_says_what_is_kept()
    {
        section("an entry whose model is not known says so, and that earlier changes are kept");

        Bench b;
        if (!b.open() || !b.add(9951, ProductClassId::Tstat10, 11))
            return;
        check(b.edit(1, offline::InputField::Label, "x"), "input 2 is changed");

        std::string message;
        Placement p;
        check(place_device(b.registry, b.db, b.handle, p, b.status, message, 0), "its model is unset");

        const std::string payload = b.payload();
        check(contains(payload, "\"unavailable\":true"), "the Inputs page has no grid");
        check(contains(payload, "Nothing was sent.") && contains(payload, "Choose its model"),
              "  and says nothing was sent, and to choose its model");
        check(contains(payload, "Changes to input 2 made earlier are kept"), "  and that the change is kept");
    }

    void test_the_requests_are_read_strictly()
    {
        section("a change and an undo are read as the page sends them, and nothing else");

        InputEditRequest r;
        std::string message;
        check(read_input_edit_request("{\"handle\":\"12\",\"index\":\"3\",\"field\":\"label\",\"value\":\"A\\u00e9\"}",
                                      r, message),
              "a change is read");
        check(r.handle == to_handle(12) && r.index == 3 && r.field == offline::InputField::Label,
              "  its handle, index and field");
        check(r.value == "A\xC3\xA9", "  and its value, unescaped to UTF-8");

        check(read_input_edit_request("{\"handle\":12,\"index\":0,\"field\":\"filter\",\"value\":\"\"}", r, message),
              "numbers as JSON numbers, and an empty value, are read");

        check(!read_input_edit_request("{\"handle\":\"12\",\"index\":\"255\",\"field\":\"label\",\"value\":\"A\"}", r, message),
              "index 255 is refused: a point's index is one byte, and 255 is not a point");
        check(!read_input_edit_request("{\"handle\":\"0\",\"index\":\"1\",\"field\":\"label\",\"value\":\"A\"}", r, message),
              "handle 0 is refused");
        check(!read_input_edit_request("{\"handle\":\"12\",\"index\":\"1\",\"field\":\"range\",\"value\":\"A\"}", r, message),
              "a field that cannot be changed is refused");
        check(contains(message, "fullLabel, autoManual, filter, label"), "  naming the ones that can");
        check(!read_input_edit_request("{\"handle\":\"12\",\"index\":\"1\",\"field\":\"label\"}", r, message),
              "a change with no value is refused");
        check(!read_input_edit_request("{\"handle\":\"12\",\"index\":\"1\",\"field\":\"filter\",\"value\":5}", r, message),
              "a value that is not a string is refused");
        check(!read_input_edit_request("{\"handle\":\"12\",\"index\":\"1\",\"field\":\"label\",\"value\":\"A\",\"value\":\"B\"}",
                                       r, message),
              "a key given twice is refused");

        Handle h = kNoHandle;
        int index = -1;
        check(read_input_revert_request("{\"handle\":\"7\",\"index\":\"63\"}", h, index, message) &&
                  h == to_handle(7) && index == 63,
              "an undo is read");
        check(!read_input_revert_request("{\"handle\":\"7\"}", h, index, message), "an undo with no index is refused");
    }
}

int run_offline_inputs_tests()
{
    test_which_devices_are_configured_offline();
    test_a_new_configuration_is_t3000s();
    test_a_change_is_saved_with_what_it_started_from();
    test_a_refused_change_saves_nothing();
    test_changes_are_refused_where_they_would_not_be_kept_or_shown();
    test_undo_puts_an_input_back();
    test_the_configuration_outlives_a_restart_and_a_scan();
    test_forgetting_a_device_takes_its_configuration();
    test_the_model_can_be_chosen_after_adding();
    test_a_model_change_keeps_changes_in_view();
    test_a_device_without_a_model_says_what_is_kept();
    test_the_requests_are_read_strictly();
    return 0;
}
