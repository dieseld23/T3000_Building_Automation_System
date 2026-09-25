// Tests for the device list as the routes change it: the registry and the
// saved file together, across scans and across restarts.
//
// A "restart" here is a second Registry and DeviceDb opened on the same file,
// which is all a restart is to this code.

#include "device_list.h"
#include "../testing/check.h"
#include "../testing/temp_file.h"

namespace
{
    using namespace t5000::app;
    using namespace t5000::device;
    using namespace t5000::testing;
    namespace store     = t5000::store;
    namespace discovery = t5000::discovery;

    DeviceRecord answered(uint32_t serial, int modbus_id = 0)
    {
        DeviceRecord d;
        d.serial_number                = serial;
        d.product                      = ProductClassId::MiniPanelArm;
        d.firmware                     = 538;
        d.modbus_id_reported           = modbus_id;
        d.connection.host              = "127.0.0.2";
        d.connection.modbus_slave_id   = modbus_id;
        d.answered_from                = "127.0.0.2";
        d.reported_ip                  = "127.0.0.2";
        d.address_note                 = "127.0.0.2";
        d.panel_name                   = "Panel " + std::to_string(serial);
        d.provenance                   = Provenance::BacnetBroadcast;
        d.reached                      = true;
        d.observation_complete         = true;
        return d;
    }

    discovery::ScanResult a_scan(std::vector<DeviceRecord> devices)
    {
        discovery::ScanResult r;
        r.devices = std::move(devices);
        r.stats.responses_parsed = (int)r.devices.size();
        return r;
    }

    const DeviceRecord* by_serial(const Registry& reg, uint32_t serial)
    {
        for (const auto& d : reg.devices())
            if (d.serial_number == serial)
                return &d;
        return nullptr;
    }

    bool has_duplicate_repair(const DeviceRecord& d)
    {
        for (const auto& r : d.repairs)
            if (r.kind == RepairKind::ResolveDuplicateModbusId)
                return true;
        return false;
    }

    std::vector<DeviceRecord> saved(store::DeviceDb& db)
    {
        std::vector<DeviceRecord> out;
        std::string error;
        check(db.load(out, error), "the saved list reads back");
        return out;
    }

    Placement a_placement()
    {
        Placement p;
        p.name     = "Boiler";
        p.building = "North";
        p.floor    = "2";
        p.room     = "Plant";
        return p;
    }

    void test_a_list_that_cannot_be_opened_is_not_fatal()
    {
        section("a list that cannot be opened leaves T5000 running, and says so");

        // A folder that does not exist, inside one that does.
        TempFile beside(L"missing");
        const std::string path = beside.utf8() + "\\no such folder\\T5000.db";

        store::DeviceDb db;
        Registry reg;
        StoreStatus status = open_saved_list(db, path, reg);

        check(!status.saving, "it is not saving");
        check(!status.error.empty(), "and says why");
        check(status.path == path, "and which file it tried");
        check(!db.is_open(), "the database is closed");

        ScanSummary summary;
        record_scan(reg, db, a_scan({ answered(8001) }), 100, summary, status);
        check_eq(reg.size(), 1, "a scan still lists what answered");
        check(!status.error.empty(), "and the reason it is not saved stays on show");

        std::string message;
        check(!place_device(reg, db, reg.devices()[0].handle, a_placement(), status, message),
              "naming a device is refused, since the name could not be kept");
        check(message.find("not being saved") != std::string::npos, "and the message says why");
        check(reg.devices()[0].placement.empty(), "and the name is not taken in memory either");

        check(forget_device(reg, db, reg.devices()[0].handle, summary, message),
              "forgetting still works, on the list in memory");
        check_eq(reg.size(), 0, "and empties it");
    }

    void test_a_scan_is_saved_and_comes_back()
    {
        section("what a scan finds is saved, and is there after a restart");

        TempFile file(L"restart");
        {
            store::DeviceDb db;
            Registry reg;
            StoreStatus status = open_saved_list(db, file.utf8(), reg);
            if (!require(status.saving, "a new list opens"))
                return;
            check_eq(status.restored, 0, "with nothing in it");

            ScanSummary summary;
            record_scan(reg, db, a_scan({ answered(8001), answered(8002) }), 100, summary, status);
            check(status.error.empty(), "the scan is saved");
            check_eq(summary.stats.responses_parsed, 2, "and its numbers reach the summary");

            const DeviceRecord* d = by_serial(reg, 8001);
            if (require(d != nullptr, "the device is listed"))
            {
                check_eq(d->answered_scan, 1, "as having answered scan 1");
                check(reg.answered_last_scan(*d), "the last scan");
                check_eq((long)d->first_seen, 100, "first seen now");
                check_eq((long)d->last_seen, 100, "and last seen now");
            }
        }

        store::DeviceDb db;
        Registry reg;
        const StoreStatus status = open_saved_list(db, file.utf8(), reg);
        check(status.saving, "the list reopens");
        check_eq(status.restored, 2, "with both devices");
        check_eq(reg.size(), 2, "in the registry");
        check_eq(reg.scan_count(), 0, "before anything has been scanned");

        for (const auto& d : reg.devices())
        {
            check(d.provenance == Provenance::Restored, "each is marked as from the saved list");
            check(!reg.answered_last_scan(d), "and not as having answered anything yet");
            check_eq((long)d.last_seen, 100, "with when it was last seen");
            check(d.handle != kNoHandle, "and a handle to select it by");
        }
    }

    void test_answered_is_per_scan()
    {
        section("each device says whether it answered the last scan, and when it was last seen");

        store::DeviceDb db;
        Registry reg;
        StoreStatus status = open_saved_list(db, ":memory:", reg);
        ScanSummary summary;

        record_scan(reg, db, a_scan({ answered(8001), answered(8002) }), 100, summary, status);
        record_scan(reg, db, a_scan({ answered(8001) }), 200, summary, status);

        const DeviceRecord* a = by_serial(reg, 8001);
        const DeviceRecord* b = by_serial(reg, 8002);
        if (!require(a && b, "both are listed"))
            return;

        check(reg.answered_last_scan(*a), "the one that answered again did");
        check(!reg.answered_last_scan(*b), "the one that did not, did not");
        check(b->answered_scan != 0, "though it was seen earlier this session");
        check_eq((long)a->last_seen, 200, "last seen at the second scan");
        check_eq((long)a->first_seen, 100, "first seen at the first");
        check_eq((long)b->last_seen, 100, "the quiet one last seen at the first");

        const auto rows = saved(db);
        for (const auto& d : rows)
            if (d.serial_number == 8002)
                check_eq((long)d.last_seen, 100, "and the file agrees about the quiet one");
    }

    void test_a_later_scan_saves_what_an_earlier_one_did_not()
    {
        section("a later scan saves every device seen this session, not only its own");

        // A save that failed leaves devices listed but not in the file. The
        // row going missing is how that is simulated here.
        store::DeviceDb db;
        Registry reg;
        StoreStatus status = open_saved_list(db, ":memory:", reg);
        ScanSummary summary;
        record_scan(reg, db, a_scan({ answered(8001), answered(8002) }), 100, summary, status);

        std::string error;
        check(db.forget(8001, error), "8001's row is lost");

        status.error = "the last scan could not be saved: disk full";
        record_scan(reg, db, a_scan({ answered(8002) }), 200, summary, status);
        check(status.error.empty(), "a save that works clears the error");

        const auto rows = saved(db);
        check_eq((long)rows.size(), 2, "and it is true: both devices are in the file");
        for (const auto& d : rows)
        {
            if (d.serial_number == 8001)
                check_eq((long)d.last_seen, 100, "the one not in this scan keeps its own last sighting");
            if (d.serial_number == 8002)
                check_eq((long)d.last_seen, 200, "the one in it has the new one");
        }
    }

    void test_a_restored_device_is_not_accused_of_a_duplicate()
    {
        section("a device known only from the saved list is not in conflict with anything");

        TempFile file(L"dup");
        {
            store::DeviceDb db;
            Registry reg;
            StoreStatus status = open_saved_list(db, file.utf8(), reg);
            ScanSummary summary;
            record_scan(reg, db, a_scan({ answered(8001, 5) }), 100, summary, status);
        }

        store::DeviceDb db;
        Registry reg;
        StoreStatus status = open_saved_list(db, file.utf8(), reg);
        ScanSummary summary;

        // Another building, another day: a different device on the same id.
        record_scan(reg, db, a_scan({ answered(9001, 5) }), 500, summary, status);

        const DeviceRecord* old_one = by_serial(reg, 8001);
        const DeviceRecord* new_one = by_serial(reg, 9001);
        if (!require(old_one && new_one, "both are listed"))
            return;
        check(!has_duplicate_repair(*old_one), "the saved one is not accused");
        check(!has_duplicate_repair(*new_one), "and neither is the one that answered");
        check_eq(summary.stats.duplicate_modbus_ids, 0, "and the scan reports no duplicates");

        // Both on the network at once is a real conflict.
        record_scan(reg, db, a_scan({ answered(8001, 5), answered(9001, 5) }), 600, summary, status);
        old_one = by_serial(reg, 8001);
        new_one = by_serial(reg, 9001);
        if (!require(old_one && new_one, "both still listed"))
            return;
        check(has_duplicate_repair(*old_one), "once both answer, the first is flagged");
        check(has_duplicate_repair(*new_one), "and so is the second");
        check_eq(summary.stats.duplicate_modbus_ids, 2, "and the scan reports both");
    }

    void test_a_restored_device_that_answers_keeps_its_history()
    {
        section("a saved device that answers again keeps its name and its first sighting");

        TempFile file(L"history");
        {
            store::DeviceDb db;
            Registry reg;
            StoreStatus status = open_saved_list(db, file.utf8(), reg);
            ScanSummary summary;
            record_scan(reg, db, a_scan({ answered(8001) }), 100, summary, status);

            std::string message;
            check(place_device(reg, db, reg.devices()[0].handle, a_placement(), status, message),
                  "it is named");
        }

        {
            store::DeviceDb db;
            Registry reg;
            StoreStatus status = open_saved_list(db, file.utf8(), reg);
            ScanSummary summary;

            DeviceRecord later = answered(8001);
            later.firmware = 540;
            record_scan(reg, db, a_scan({ later }), 500, summary, status);

            const DeviceRecord* d = by_serial(reg, 8001);
            if (!require(d != nullptr, "it is listed once"))
                return;
            check_eq(reg.size(), 1, "and only once");
            check(d->provenance == Provenance::BacnetBroadcast, "as having answered, not as restored");
            check(d->placement.name == "Boiler", "with its name");
            check_eq((long)d->first_seen, 100, "its first sighting");
            check_eq((long)d->last_seen, 500, "and the new one");
            check_eq(d->firmware, 540, "and the firmware it has now");
        }

        store::DeviceDb db;
        Registry reg;
        open_saved_list(db, file.utf8(), reg);
        const DeviceRecord* d = by_serial(reg, 8001);
        if (require(d != nullptr, "after another restart it is there"))
        {
            check(d->placement.room == "Plant", "still named and placed");
            check_eq((long)d->first_seen, 100, "first seen at 100");
            check_eq((long)d->last_seen, 500, "last seen at 500");
            check_eq(d->firmware, 540, "with the firmware from the later scan");
        }
    }

    void test_forgetting_a_device()
    {
        section("a forgotten device leaves the list and the file, and nothing else");

        store::DeviceDb db;
        Registry reg;
        StoreStatus status = open_saved_list(db, ":memory:", reg);
        ScanSummary summary;
        record_scan(reg, db, a_scan({ answered(8001), answered(8002) }), 100, summary, status);

        const Handle gone = by_serial(reg, 8001)->handle;
        reg.select_by_handle(gone);

        std::string message;
        check(forget_device(reg, db, gone, summary, message), "it is forgotten");
        check_eq(reg.size(), 1, "one device is left");
        check(reg.selected() == nullptr, "and the selection went with it, rather than sliding");
        check_eq((long)saved(db).size(), 1, "the file holds one");

        check(!forget_device(reg, db, gone, summary, message), "forgetting it twice is refused");
        check(message.find("no longer in the list") != std::string::npos, "and says it has gone");

        record_scan(reg, db, a_scan({ answered(8001) }), 200, summary, status);
        const DeviceRecord* back = by_serial(reg, 8001);
        if (require(back != nullptr, "when it answers again it is listed again"))
            check(back->handle != gone, "under a new handle, never the old one");
        check_eq((long)saved(db).size(), 2, "and saved again");
    }

    void test_forgetting_one_of_a_duplicate_pair_clears_the_other()
    {
        section("forgetting one of two devices on the same id stops the other being accused");

        // Found on the page: the survivor went on saying "Modbus id 7 is
        // claimed by 2 devices" beside a list with one device on id 7.
        store::DeviceDb db;
        Registry reg;
        StoreStatus status = open_saved_list(db, ":memory:", reg);
        ScanSummary summary;
        record_scan(reg, db, a_scan({ answered(8001, 7), answered(8002, 7) }), 100, summary, status);
        check_eq(summary.stats.duplicate_modbus_ids, 2, "both are flagged");

        std::string message;
        check(forget_device(reg, db, by_serial(reg, 8002)->handle, summary, message), "one is forgotten");

        const DeviceRecord* left = by_serial(reg, 8001);
        if (require(left != nullptr, "the other is still listed"))
            check(!has_duplicate_repair(*left), "and no longer accused");
        check_eq(summary.stats.duplicate_modbus_ids, 0, "and the count the banner shows agrees");
        check(reg.pending_repairs().empty(), "no problem is left pending");
    }

    void test_forgetting_every_device()
    {
        section("forgetting every device empties the list and the file");

        store::DeviceDb db;
        Registry reg;
        StoreStatus status = open_saved_list(db, ":memory:", reg);
        ScanSummary summary;
        record_scan(reg, db, a_scan({ answered(8001), answered(8002) }), 100, summary, status);

        std::string message;
        check(forget_all(reg, db, message), "they are forgotten");
        check_eq(reg.size(), 0, "the list is empty");
        check_eq((long)saved(db).size(), 0, "and so is the file");
    }

    void test_a_device_with_no_serial_is_listed_not_saved()
    {
        section("a device with no serial is listed, and cannot be saved or named");

        store::DeviceDb db;
        Registry reg;
        StoreStatus status = open_saved_list(db, ":memory:", reg);
        ScanSummary summary;
        record_scan(reg, db, a_scan({ answered(0), answered(8001) }), 100, summary, status);

        check_eq(reg.size(), 2, "both are listed");
        check(status.error.empty(), "the scan saves without complaint");
        check_eq((long)saved(db).size(), 1, "but only the one with a serial is saved");

        const DeviceRecord* unkeyed = by_serial(reg, 0);
        if (!require(unkeyed != nullptr, "the unkeyed device is there"))
            return;

        std::string message;
        check(!place_device(reg, db, unkeyed->handle, a_placement(), status, message),
              "naming it is refused");
        check(message.find("no serial") != std::string::npos, "because it has no serial");

        check(forget_device(reg, db, unkeyed->handle, summary, message), "it can still be taken off the list");
        check_eq(reg.size(), 1, "and is");
    }

    void test_naming_a_device()
    {
        section("a name and location are tidied, limited, and saved");

        store::DeviceDb db;
        Registry reg;
        StoreStatus status = open_saved_list(db, ":memory:", reg);
        ScanSummary summary;
        record_scan(reg, db, a_scan({ answered(8001) }), 100, summary, status);
        const Handle h = reg.devices()[0].handle;

        Placement p;
        p.name     = "  Boiler  ";
        p.building = "\tNorth ";
        std::string message;
        check(place_device(reg, db, h, p, status, message), "a padded name is taken");
        check(reg.devices()[0].placement.name == "Boiler", "trimmed");
        check(reg.devices()[0].placement.building == "North", "all of it");

        const auto rows = saved(db);
        if (require(rows.size() == 1, "the file has the device"))
            check(rows[0].placement.name == "Boiler", "with the trimmed name");

        Placement too_long;
        too_long.room = std::string(kMaxPlacementChars + 1, 'x');
        check(!place_device(reg, db, h, too_long, status, message), "a name one past the limit is refused");
        check(message.find("room") != std::string::npos, "naming the field");
        check(reg.devices()[0].placement.name == "Boiler", "and nothing changes");

        // Characters, not bytes: 60 accented letters are 120 bytes.
        Placement accents;
        for (int i = 0; i < kMaxPlacementChars; i++)
            accents.room += "\xC3\xA9";
        check(place_device(reg, db, h, accents, status, message),
              "60 accented letters are within the limit");

        Placement control;
        control.name = std::string("Boiler\x01", 7);
        check(!place_device(reg, db, h, control, status, message), "a control character is refused");

        Placement broken;
        broken.floor = "\xC3";
        check(!place_device(reg, db, h, broken, status, message), "half a UTF-8 character is refused");

        Placement overlong;
        overlong.floor = "\xC0\xAF";
        check(!place_device(reg, db, h, overlong, status, message), "an overlong UTF-8 form is refused");

        check(!place_device(reg, db, to_handle(987654), a_placement(), status, message),
              "a handle that is not in the list is refused");
    }

    void test_a_typed_name_is_read_as_typed()
    {
        section("a name comes off the wire as typed, whatever it says");

        Handle h = kNoHandle;
        Placement p;
        std::string message;

        // The key-search reader would find "floor" as the NAME here and read
        // the building as the floor.
        check(read_placement_request(
                  "{\"handle\":\"7\",\"name\":\"floor\",\"building\":\"B\",\"floor\":\"3\","
                  "\"room\":\"Plant \\\"A\\\" \\\\ \\u00e9 \\ud83d\\ude00\"}",
                  h, p, message),
              "a body with awkward text in it is read");
        check_eq((long)to_number(h), 7, "the handle");
        check(p.name == "floor", "a name that is also a key");
        check(p.building == "B", "the building, not confused with it");
        check(p.floor == "3", "the floor");
        check(p.room == "Plant \"A\" \\ \xC3\xA9 \xF0\x9F\x98\x80", "quotes, a backslash, an accent and an emoji");

        check(read_placement_request("{\"handle\":12}", h, p, message), "a handle may be a number");
        check(p.empty(), "and fields left out are left empty");

        check(!read_placement_request("{\"name\":\"x\"}", h, p, message), "no handle is refused");
        check(!read_placement_request("{\"handle\":\"-1\"}", h, p, message), "a negative one");
        check(!read_placement_request("{\"handle\":\"0\"}", h, p, message), "handle 0");
        check(!read_placement_request("{\"handle\":\"7\",\"name\":5}", h, p, message), "a name that is not text");
        check(!read_placement_request("{\"handle\":\"7\",\"name\":\"a\",\"name\":\"b\"}", h, p, message),
              "a name given twice");
        check(!read_placement_request("not json", h, p, message), "something that is not JSON");
        check(message.find("could not be read") != std::string::npos, "and the message says so");

        check(read_handle_request("{\"handle\":\"12\"}", h, message), "a forget request is read");
        check_eq((long)to_number(h), 12, "with its handle");
        check(!read_handle_request("{}", h, message), "and refused without one");
    }
}

namespace
{
    // ------------------------------------------------------ added by hand

    HandAdded typed_in(uint32_t serial, ProductClassId product = ProductClassId::Esp32T3Series)
    {
        HandAdded d;
        d.serial    = serial;
        d.product   = product;
        d.placement = a_placement();
        return d;
    }

    void test_adding_a_device_by_hand()
    {
        section("a device added by hand is listed, saved, and marked as added by hand");

        TempFile file(L"add");
        Handle added = kNoHandle;
        {
            store::DeviceDb db;
            Registry reg;
            StoreStatus status = open_saved_list(db, file.utf8(), reg);
            if (!require(status.saving, "a new list opens"))
                return;

            HandAdded d = typed_in(8101);
            d.placement.name = "  Boiler  ";
            std::string message;
            check(add_device(reg, db, d, status, added, message), "a device is added by hand");
            check(message.empty(), "  with nothing to explain");

            const DeviceRecord* r = by_serial(reg, 8101);
            if (require(r != nullptr, "it is listed"))
            {
                check(r->handle == added && added != kNoHandle, "  under the handle returned");
                check(r->product == ProductClassId::Esp32T3Series, "  as the product given");
                check(r->provenance == Provenance::ManuallyAdded, "  as added by hand");
                check(!r->reached, "  and never reached");
                check(r->connection.host.empty(), "  with no address");
                check(r->placement.name == "Boiler", "  named, the name tidied as any is");
                check(r->placement.building == "North", "  and placed");
                check_eq(r->answered_scan, 0, "  and not having answered a scan");
            }
            check(reg.selected() == nullptr, "adding a device does not select it");
        }

        store::DeviceDb db;
        Registry reg;
        const StoreStatus status = open_saved_list(db, file.utf8(), reg);
        check_eq(status.restored, 1, "after a restart it is back");
        const DeviceRecord* r = by_serial(reg, 8101);
        if (require(r != nullptr, "  by its serial"))
        {
            check(r->provenance == Provenance::ManuallyAdded, "  still as added by hand, not as seen");
            check(r->placement.name == "Boiler", "  with its name");
        }
    }

    void test_what_adding_by_hand_refuses()
    {
        section("adding by hand refuses what could never become the device, and changes nothing");

        store::DeviceDb db;
        Registry reg;
        StoreStatus status = open_saved_list(db, ":memory:", reg);
        ScanSummary summary;
        record_scan(reg, db, a_scan({ answered(8001) }), 100, summary, status);

        Handle h = kNoHandle;
        std::string message;

        check(!add_device(reg, db, typed_in(0), status, h, message), "serial 0 is refused");
        check(message.find("never match") != std::string::npos, "  since a scan could never match it");
        check(!add_device(reg, db, typed_in(0xFFFFFFFFu), status, h, message), "serial 0xFFFFFFFF is refused");

        check(!add_device(reg, db, typed_in(8102, ProductClassId::Unknown), status, h, message),
              "no product is refused");
        check(!add_device(reg, db, typed_in(8102, ProductClassId::Tstat5B), status, h, message),
              "a product T5000 has not been taught about is refused");
        check(message.find("Pick one") != std::string::npos, "  and the page is told to pick from the list");

        check(!add_device(reg, db, typed_in(8001, ProductClassId::Cm5), status, h, message),
              "a serial already in the list is refused");
        check(message.find("already in the list") != std::string::npos, "  and says so");
        const DeviceRecord* scanned = by_serial(reg, 8001);
        if (require(scanned != nullptr, "the device already listed is still there"))
        {
            check(scanned->product == ProductClassId::MiniPanelArm, "  with the product it reported");
            check(scanned->placement.empty(), "  and none of the typed-in placement");
            check(scanned->provenance == Provenance::BacnetBroadcast, "  and as having answered");
        }

        HandAdded long_name = typed_in(8103);
        long_name.placement.room = std::string(kMaxPlacementChars + 1, 'x');
        check(!add_device(reg, db, long_name, status, h, message), "a room one past the limit is refused");
        check(message.find("room") != std::string::npos, "  naming the field");

        check(h == kNoHandle, "no refusal hands back a handle");
        check_eq(reg.size(), 1, "and the list still holds only the scanned device");
        check_eq((long)saved(db).size(), 1, "  as does the file");

        check(add_device(reg, db, typed_in(8104), status, h, message), "a new serial is added");
        check(!add_device(reg, db, typed_in(8104), status, h, message), "  and not twice");
        check(message.find("added by hand") != std::string::npos, "  and the reason says how it got there");
    }

    void test_adding_by_hand_is_refused_when_nothing_is_saved()
    {
        section("a device is not added by hand when the list is not being saved");

        TempFile beside(L"unsaved");
        const std::string path = beside.utf8() + "\\no such folder\\T5000.db";

        store::DeviceDb db;
        Registry reg;
        StoreStatus status = open_saved_list(db, path, reg);

        Handle h = kNoHandle;
        std::string message;
        check(!add_device(reg, db, typed_in(8101), status, h, message), "it is refused");
        check(message.find("not being saved") != std::string::npos, "  since it would be gone when T5000 closes");
        check_eq(reg.size(), 0, "  and is not listed in memory either");
    }

    void test_a_scan_finds_a_device_added_by_hand()
    {
        section("a scan that finds a device added by hand puts it in the entry's place");

        TempFile file(L"found");
        {
            store::DeviceDb db;
            Registry reg;
            StoreStatus status = open_saved_list(db, file.utf8(), reg);

            Handle h = kNoHandle;
            std::string message;
            check(add_device(reg, db, typed_in(8101), status, h, message), "a device is added by hand");
            check(add_device(reg, db, typed_in(8102), status, h, message), "and another");

            ScanSummary summary;
            record_scan(reg, db, a_scan({ answered(8101) }), 500, summary, status);
            check(status.error.empty(), "a scan finds the first, and is saved");
            check_eq(reg.size(), 2, "still two devices, not three");

            const DeviceRecord* found = by_serial(reg, 8101);
            if (require(found != nullptr, "the one found is listed"))
            {
                check(found->provenance == Provenance::BacnetBroadcast, "  as having answered");
                check(found->product == ProductClassId::MiniPanelArm, "  as the product it reported");
                check(found->connection.host == "127.0.0.2", "  at the address it answered from");
                check(found->placement.name == "Boiler", "  keeping the name given by hand");
                check_eq((long)found->first_seen, 500, "  first seen by that scan");
            }

            const DeviceRecord* other = by_serial(reg, 8102);
            if (require(other != nullptr, "the other is still listed"))
                check(other->provenance == Provenance::ManuallyAdded, "  as added by hand");
        }

        store::DeviceDb db;
        Registry reg;
        open_saved_list(db, file.utf8(), reg);
        const DeviceRecord* found = by_serial(reg, 8101);
        if (require(found != nullptr, "after a restart the one found is back"))
        {
            check(found->provenance == Provenance::Restored, "  as a device that has answered");
            check_eq((long)found->first_seen, 500, "  with when it was first seen");
            check(found->placement.name == "Boiler", "  and its name");
        }
        const DeviceRecord* other = by_serial(reg, 8102);
        if (require(other != nullptr, "the other is back"))
            check(other->provenance == Provenance::ManuallyAdded, "  still as added by hand");
    }

    void test_an_add_request_is_read()
    {
        section("the page's request to add a device is read, numbers as numbers or as text");

        HandAdded d;
        std::string message;
        check(read_add_request("{\"productId\":88,\"serialNumber\":\"123456\",\"name\":\"AHU\","
                               "\"building\":\"North\",\"floor\":\"2\",\"room\":\"Plant\"}",
                               d, message),
              "a full request is read");
        check(d.product == ProductClassId::Esp32T3Series, "  the product");
        check_eq((long)d.serial, 123456, "  the serial, sent as text");
        check(d.placement.name == "AHU" && d.placement.room == "Plant", "  and the placement");

        check(read_add_request("{\"productId\":\"74\",\"serialNumber\":4294967294}", d, message),
              "numbers either way, and no placement");
        check_eq((long)(d.serial == 4294967294u), 1, "  the largest serial that is a key");
        check(d.placement.empty(), "  with the placement left empty");

        check(!read_add_request("{\"serialNumber\":\"1\"}", d, message), "no product is refused");
        check(message == "Choose a product.", "  in words for the form, not the field's name");
        check(!read_add_request("{\"productId\":\"\",\"serialNumber\":\"1\"}", d, message),
              "  as is the list left on its first line");
        check(!read_add_request("{\"productId\":74}", d, message), "no serial is refused");
        check(message.find("serial number") != std::string::npos, "  in words for the form");
        check(!read_add_request("{\"productId\":74,\"serialNumber\":\"\"}", d, message), "an empty serial is refused");
        check(!read_add_request("{\"productId\":74,\"serialNumber\":\"12 34\"}", d, message),
              "a serial with a space in it is refused");
        check(!read_add_request("{\"productId\":74,\"serialNumber\":-5}", d, message), "a negative serial is refused");
        check(!read_add_request("{\"productId\":74,\"serialNumber\":4294967296}", d, message),
              "a serial too big for the device's four bytes is refused");
        check(!read_add_request("{\"productId\":256,\"serialNumber\":1}", d, message), "a product past 255 is refused");
        check(!read_add_request("{\"productId\":74,\"serialNumber\":1,\"name\":5}", d, message),
              "a name that is not text is refused");
        check(!read_add_request("not json", d, message), "and a body that is not JSON");
    }
}

int run_device_list_tests()
{
    test_a_list_that_cannot_be_opened_is_not_fatal();
    test_a_scan_is_saved_and_comes_back();
    test_answered_is_per_scan();
    test_a_later_scan_saves_what_an_earlier_one_did_not();
    test_a_restored_device_is_not_accused_of_a_duplicate();
    test_a_restored_device_that_answers_keeps_its_history();
    test_forgetting_a_device();
    test_forgetting_one_of_a_duplicate_pair_clears_the_other();
    test_forgetting_every_device();
    test_a_device_with_no_serial_is_listed_not_saved();
    test_naming_a_device();
    test_a_typed_name_is_read_as_typed();
    test_adding_a_device_by_hand();
    test_what_adding_by_hand_refuses();
    test_adding_by_hand_is_refused_when_nothing_is_saved();
    test_a_scan_finds_a_device_added_by_hand();
    test_an_add_request_is_read();
    return 0;
}
