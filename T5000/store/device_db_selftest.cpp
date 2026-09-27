// Tests for the saved device list.
//
// Most run against a database in memory. The ones about the FILE - that the
// list survives being closed and reopened, and that a file this build should
// not touch is left exactly as it was - use a real one in %TEMP%.
//
// The property most worth guarding is the split between what a scan learns
// and what the operator typed. A rescan that blanked a building's worth of
// room names would pass every check that only looked at the scanned fields.

#include "device_db.h"
#include "sqlite.h"
#include "../testing/check.h"
#include "../testing/temp_file.h"

namespace
{
    using namespace t5000::store;
    using namespace t5000::device;
    using namespace t5000::testing;

    DeviceRecord scanned(uint32_t serial, int64_t when = 1000)
    {
        DeviceRecord d;
        d.serial_number                = serial;
        d.product                      = ProductClassId::MiniPanelArm;
        d.mini_type                    = 7;
        d.firmware                     = 538;
        d.modbus_id_reported           = 12;
        d.parent_serial                = 0;
        d.connection.host              = "127.0.0.2";
        d.connection.udp_port          = 47809;
        d.connection.device_instance   = 4321;
        d.connection.modbus_slave_id   = 12;
        d.answered_from                = "127.0.0.2";
        d.reported_ip                  = "192.168.0.3";
        d.address_note                 = "127.0.0.2";
        d.panel_name                   = "AHU 3";
        d.provenance                   = Provenance::BacnetBroadcast;
        d.reached                      = true;
        d.observation_complete         = true;
        d.first_seen                   = when;
        d.last_seen                    = when;
        d.answered_scan                = 1;
        return d;
    }

    Placement a_placement()
    {
        Placement p;
        p.name     = "Boiler \"B\"";
        p.building = "North";
        p.floor    = "2";
        p.room     = "Plant \xC3\xA9";   // "Plant é", UTF-8
        return p;
    }

    bool open_memory(DeviceDb& db)
    {
        std::string error;
        const bool ok = db.open(":memory:", error);
        if (!ok)
            printf("  (could not open a database in memory: %s)\n", error.c_str());
        return ok;
    }

    std::vector<DeviceRecord> load(DeviceDb& db)
    {
        std::vector<DeviceRecord> out;
        std::string error;
        check(db.load(out, error), "the saved list loads");
        return out;
    }

    int64_t single_int(Database& db, const char* sql)
    {
        Statement q(db, sql);
        return q.step() == Statement::Step::Row ? q.column_int(0) : -1;
    }

    void test_text_is_bound_not_spliced()
    {
        section("text goes into SQL as a value, never as SQL");

        Database db;
        std::string error;
        if (!require(db.open(":memory:", error), "a database opens in memory"))
            return;
        check(db.exec("CREATE TABLE t (x TEXT)", error), "a table is made");

        const std::string hostile = "Robert'); DROP TABLE t;--";
        {
            Statement q(db, "INSERT INTO t VALUES (?1)");
            q.bind(1, hostile);
            check(q.step() == Statement::Step::Done, "a name with SQL in it is inserted");
        }

        Statement q(db, "SELECT x FROM t");
        check(q.step() == Statement::Step::Row, "and the table is still there");
        check(q.column_text(0) == hostile, "holding the name exactly as given");
    }

    void test_a_broken_statement_says_why()
    {
        section("a statement that cannot run says why, and does not run");

        Database db;
        std::string error;
        if (!require(db.open(":memory:", error), "a database opens in memory"))
            return;

        Statement q(db, "SELECT nothing FROM nowhere");
        check(!q.ok(), "a query on a missing table does not prepare");
        check(!q.error().empty(), "and says why");
        check(q.step() == Statement::Step::Error, "and step refuses it");
    }

    void test_a_saved_device_comes_back_restored()
    {
        section("a saved device comes back with everything the scan learned, as restored");

        DeviceDb db;
        if (!require(open_memory(db), "the list opens"))
            return;

        std::string error;
        check(db.save_scanned({ scanned(8001) }, error), "a scanned device is saved");

        const auto list = load(db);
        if (!require(list.size() == 1, "one device comes back"))
            return;

        const DeviceRecord& d = list[0];
        check_eq((long)d.serial_number, 8001, "its serial");
        check(d.product == ProductClassId::MiniPanelArm, "its product");
        check_eq(d.mini_type, 7, "its panel type");
        check_eq(d.firmware, 538, "its firmware");
        check_eq(d.modbus_id_reported, 12, "the Modbus id it reported");
        check_eq(d.connection.modbus_slave_id, 12, "and the id it is addressed on, as a scan sets it");
        check(d.connection.host == "127.0.0.2", "the address it is reached at");
        check_eq(d.connection.udp_port, 47809, "its BACnet port");
        check_eq(d.connection.device_instance, 4321, "its BACnet instance");
        check(d.answered_from == "127.0.0.2", "where it answered from");
        check(d.reported_ip == "192.168.0.3", "and where it says it is, as a pair");
        check(d.address_mismatch(), "so the mismatch is still shown after a restart");
        check(d.address_note == "127.0.0.2", "the address the list shows");
        check(d.panel_name == "AHU 3", "the panel's own name");
        check_eq((long)d.first_seen, 1000, "when it was first seen");
        check_eq((long)d.last_seen, 1000, "and last seen");

        check(d.provenance == Provenance::Restored, "it is marked as restored");
        check_eq(d.answered_scan, 0, "and as not having answered a scan this session");
        check(!d.observation_complete, "and as not a complete look at the device");
        check(d.repairs.empty(), "repairs are not saved; a scan finds them again");
        check(d.handle == kNoHandle, "the registry, not the file, gives it a handle");
    }

    void test_a_rescan_keeps_what_the_operator_typed()
    {
        section("a rescan updates what it saw and keeps the name, location and first sighting");

        DeviceDb db;
        if (!require(open_memory(db), "the list opens"))
            return;

        std::string error;
        DeviceRecord first = scanned(8001, 1000);
        check(db.save_scanned({ first }, error), "saved from the first scan");

        first.placement = a_placement();
        check(db.save_placement(first, error), "then named and placed");

        // The next scan's record, as the registry holds it after a merge -
        // except that it carries no placement, as a scan never does.
        DeviceRecord later  = scanned(8001, 2000);
        later.firmware      = 540;
        later.connection.host = "127.0.0.3";
        later.answered_from = "127.0.0.3";
        check(db.save_scanned({ later }, error), "saved from a later scan");

        const auto list = load(db);
        if (!require(list.size() == 1, "still one device"))
            return;

        const DeviceRecord& d = list[0];
        check_eq(d.firmware, 540, "the new firmware");
        check(d.connection.host == "127.0.0.3", "the new address");
        check_eq((long)d.last_seen, 2000, "the new sighting");
        check_eq((long)d.first_seen, 1000, "the first sighting kept");
        check(d.placement.name == "Boiler \"B\"", "the name kept, quotes and all");
        check(d.placement.building == "North", "the building kept");
        check(d.placement.floor == "2", "the floor kept");
        check(d.placement.room == "Plant \xC3\xA9", "the room kept, accent and all");
    }

    void test_last_seen_does_not_go_backwards()
    {
        section("a save with an older sighting does not move last_seen back");

        DeviceDb db;
        if (!require(open_memory(db), "the list opens"))
            return;

        std::string error;
        check(db.save_scanned({ scanned(8001, 2000) }, error), "saved at 2000");
        check(db.save_scanned({ scanned(8001, 1500) }, error), "saved again at 1500");

        const auto list = load(db);
        if (require(list.size() == 1, "one device"))
            check_eq((long)list[0].last_seen, 2000, "last seen stays at 2000");
    }

    void test_a_device_with_no_serial_is_never_saved()
    {
        section("a device with no usable serial is never saved");

        DeviceDb db;
        if (!require(open_memory(db), "the list opens"))
            return;

        std::string error;
        check(db.save_scanned({ scanned(0), scanned(0xFFFFFFFFu), scanned(8001) }, error),
              "saving a mix works");
        check_eq((long)load(db).size(), 1, "only the device with a serial is kept");

        DeviceRecord unkeyed = scanned(0);
        unkeyed.placement = a_placement();
        check(!db.save_placement(unkeyed, error), "naming a device with no serial is refused");
        check(!error.empty(), "and says why");
        check_eq((long)load(db).size(), 1, "and adds nothing");
    }

    void test_the_table_itself_refuses_an_unkeyed_row()
    {
        section("the table refuses a row with no usable serial, whoever writes it");

        TempFile file(L"check");
        {
            DeviceDb db;
            std::string error;
            if (!require(db.open(file.utf8(), error), "a new list is made"))
                return;
        }

        Database raw;
        std::string error;
        if (!require(raw.open(file.utf8(), error), "and reopened directly"))
            return;

        const auto insert = [&](const char* kind, int64_t serial) {
            Statement q(raw,
                        "INSERT INTO devices (kind, serial, product_class_id, mini_type, firmware,"
                        " modbus_id, parent_serial, object_instance, host, answered_from, reported_ip,"
                        " bacnet_port, panel_name, name, building, floor, room, first_seen, last_seen)"
                        " VALUES (?1, ?2, 74, 0, 0, 0, 0, 0, '', '', '', 0, '', '', '', '', '', 0, 0)");
            q.bind(1, std::string(kind));
            q.bind(2, serial);
            return q.step() == Statement::Step::Done;
        };

        check(!insert("scanned", 0), "serial 0 is refused");
        check(!insert("scanned", 0xFFFFFFFFll), "serial 0xFFFFFFFF is refused");
        check(!insert("guessed", 8001), "an unknown kind is refused");
        check(insert("scanned", 8001), "a real serial is taken");
        check(!insert("scanned", 8001), "and not twice");
        check(insert("virtual", 8001), "a virtual device may share a serial with a scanned one");
    }

    void test_a_placement_saves_a_device_not_yet_saved()
    {
        section("naming a device that is not saved yet saves it");

        DeviceDb db;
        if (!require(open_memory(db), "the list opens"))
            return;

        DeviceRecord d = scanned(8001);
        d.placement = a_placement();

        std::string error;
        check(db.save_placement(d, error), "a device is named before any save");

        const auto list = load(db);
        if (require(list.size() == 1, "and is now saved"))
        {
            check(list[0].placement.building == "North", "with its location");
            check(list[0].panel_name == "AHU 3", "and everything the scan learned");
        }
    }

    void test_forgetting()
    {
        section("a forgotten device is gone from the file, name and all");

        DeviceDb db;
        if (!require(open_memory(db), "the list opens"))
            return;

        std::string error;
        check(db.save_scanned({ scanned(8001), scanned(8002), scanned(8003) }, error), "three saved");

        check(db.forget(scanned_key(8002), error), "one is forgotten");
        auto list = load(db);
        check_eq((long)list.size(), 2, "two remain");
        for (const auto& d : list)
            check(d.serial_number != 8002, "and the forgotten one is not among them");

        check(db.forget(scanned_key(9999), error), "forgetting a device that was never saved is not an error");

        check(db.forget_all(error), "the rest are forgotten");
        check_eq((long)load(db).size(), 0, "and the list is empty");
    }

    void test_the_list_survives_closing()
    {
        section("the list is still there after T5000 closes and starts again");

        TempFile file(L"reopen");
        {
            DeviceDb db;
            std::string error;
            if (!require(db.open(file.utf8(), error), "a new list is made"))
                return;

            check(db.path() == file.utf8(), "it knows where it is");

            DeviceRecord d = scanned(8001);
            d.placement = a_placement();
            check(db.save_scanned({ d, scanned(8002) }, error), "two devices saved");
            check(db.save_placement(d, error), "one of them named");
        }

        check(file.exists(), "the file is there, under a name with an accent in it");

        DeviceDb db;
        std::string error;
        if (!require(db.open(file.utf8(), error), "it reopens"))
            return;

        const auto list = load(db);
        if (!require(list.size() == 2, "both devices are back"))
            return;
        check_eq((long)list[0].serial_number, 8001, "in the order they were saved");
        check(list[0].placement.room == "Plant \xC3\xA9", "with the name and location typed in");
        check_eq((long)list[1].serial_number, 8002, "and the second");

        Database raw;
        if (require(raw.open(file.utf8(), error), "the file opens directly"))
            check_eq((long)single_int(raw, "PRAGMA user_version"), kSchemaVersion,
                     "and records the schema it was written with");
    }

    void test_a_newer_file_is_left_alone()
    {
        section("a list written by a newer T5000 is not used, and not changed");

        TempFile file(L"newer");
        {
            Database raw;
            std::string error;
            if (!require(raw.open(file.utf8(), error), "a file is made"))
                return;
            check(raw.exec("CREATE TABLE devices (x); PRAGMA user_version = 99;", error),
                  "as a future T5000 might have written it");
        }

        DeviceDb db;
        std::string error;
        check(!db.open(file.utf8(), error), "it is refused");
        check(error.find("newer") != std::string::npos, "and the reason names it as newer");
        check(!db.is_open(), "and it is not left open");

        Database raw;
        if (require(raw.open(file.utf8(), error), "the file still opens directly"))
        {
            check_eq((long)single_int(raw, "PRAGMA user_version"), 99, "with its version untouched");
            Statement q(raw, "SELECT x FROM devices");
            check(q.ok(), "and its table as it was");
        }
    }

    void test_someone_elses_database_is_left_alone()
    {
        section("a SQLite file that is not a T5000 list, such as T3000's, is not written to");

        TempFile file(L"foreign");
        {
            Database raw;
            std::string error;
            if (!require(raw.open(file.utf8(), error), "a file is made"))
                return;
            check(raw.exec("CREATE TABLE ALL_NODE (Serial_ID TEXT)", error),
                  "with one of T3000's tables in it");
        }

        DeviceDb db;
        std::string error;
        check(!db.open(file.utf8(), error), "it is refused");
        check(error.find("not a T5000") != std::string::npos, "and the reason says whose it is not");

        Database raw;
        if (require(raw.open(file.utf8(), error), "the file still opens directly"))
        {
            check_eq((long)single_int(raw, "SELECT count(*) FROM sqlite_master"), 1,
                     "and holds only the table it had");
            check_eq((long)single_int(raw, "PRAGMA user_version"), 0, "at the version it had");
        }
    }

    void test_a_foreign_database_claiming_our_version_is_left_alone()
    {
        section("a database that says version 1 but has no device table is not taken for ours");

        TempFile file(L"claims");
        {
            Database raw;
            std::string error;
            if (!require(raw.open(file.utf8(), error), "a file is made"))
                return;
            check(raw.exec("CREATE TABLE ALL_NODE (Serial_ID TEXT); PRAGMA user_version = 1;", error),
                  "with someone else's table and our version number");
        }

        DeviceDb db;
        std::string error;
        check(!db.open(file.utf8(), error), "it is refused");
        check(error.find("not a T5000") != std::string::npos, "as not a T5000 list");

        Database raw;
        if (require(raw.open(file.utf8(), error), "the file still opens directly"))
            check_eq((long)single_int(raw, "SELECT count(*) FROM sqlite_master"), 1,
                     "and holds only the table it had");
    }

    void test_a_file_that_is_not_sqlite_is_left_alone()
    {
        section("a file that is not a database at all is refused and not changed");

        TempFile file(L"text");
        std::string text;
        for (int i = 0; i < 64; i++)
            text += "This is a note someone saved under the wrong name.\r\n";
        if (!require(file.write(text), "a text file is made"))
            return;

        DeviceDb db;
        std::string error;
        check(!db.open(file.utf8(), error), "it is refused");
        check(!error.empty(), "and says why");
        check(file.read() == text, "and its contents are exactly as they were");
    }

    // ------------------------------------------------------ added by hand

    // What app::add_device saves: the operator's serial, product and
    // placement, and nothing a scan learned.
    DeviceRecord typed_in(uint32_t serial)
    {
        DeviceRecord d;
        d.serial_number = serial;
        d.product       = ProductClassId::Esp32T3Series;
        d.provenance    = Provenance::ManuallyAdded;
        d.placement     = a_placement();
        return d;
    }

    void test_a_device_added_by_hand_comes_back_as_added_by_hand()
    {
        section("a device added by hand is saved, and comes back as added by hand, not as seen");

        DeviceDb db;
        if (!require(open_memory(db), "the list opens"))
            return;

        std::string error;
        check(db.add_by_hand(typed_in(8101), error), "a device is added by hand");

        const auto list = load(db);
        if (!require(list.size() == 1, "it comes back"))
            return;

        const DeviceRecord& d = list[0];
        check_eq((long)d.serial_number, 8101, "its serial");
        check(d.product == ProductClassId::Esp32T3Series, "the product it was added as");
        check(d.placement.name == "Boiler \"B\"" && d.placement.room == "Plant \xC3\xA9",
              "its name and location");
        check(d.provenance == Provenance::ManuallyAdded, "as added by hand, not as restored");
        check(!d.reached, "and as never reached");
        check(d.connection.host.empty() && d.address_note.empty(), "with no address");
        check_eq((long)d.first_seen, 0, "never seen");
        check_eq((long)d.last_seen, 0, "  first or last");
    }

    void test_adding_by_hand_never_overwrites_a_saved_device()
    {
        section("adding by hand only ever adds: a saved serial is refused and left as it was");

        DeviceDb db;
        if (!require(open_memory(db), "the list opens"))
            return;

        std::string error;
        check(db.save_scanned({ scanned(8001) }, error), "a scanned device is saved");

        DeviceRecord same = typed_in(8001);
        check(!db.add_by_hand(same, error), "adding its serial by hand is refused");
        check(error.find("already") != std::string::npos, "  and says it is already there");

        const auto list = load(db);
        if (require(list.size() == 1, "still one device"))
        {
            check(list[0].product == ProductClassId::MiniPanelArm, "  with the product the device reported");
            check(list[0].connection.host == "127.0.0.2", "  and the address it answered from");
            check(list[0].placement.empty(), "  and none of the typed-in placement");
        }

        error.clear();
        check(db.add_by_hand(typed_in(8101), error), "a new serial is added");
        check(!db.add_by_hand(typed_in(8101), error), "and not twice");
        check(error.find("added by hand") != std::string::npos, "  and the reason says how it got there");

        error.clear();
        check(!db.add_by_hand(typed_in(0), error), "serial 0 is refused");
        check(error.find("no serial number") != std::string::npos,
              "  before the table's own check, in words for the page");
        check(!db.add_by_hand(typed_in(0xFFFFFFFFu), error), "serial 0xFFFFFFFF is refused");
        check_eq((long)load(db).size(), 2, "and nothing else was added");
    }

    void test_a_scan_that_finds_a_device_added_by_hand()
    {
        section("a scan that finds a device added by hand updates its row and keeps its name");

        TempFile file(L"byhand");
        {
            DeviceDb db;
            std::string error;
            if (!require(db.open(file.utf8(), error), "a new list is made"))
                return;
            check(db.add_by_hand(typed_in(8101), error), "a device is added by hand");

            // As the registry holds it after the merge: what the scan saw,
            // and the placement the entry had.
            DeviceRecord found = scanned(8101, 3000);
            found.placement    = a_placement();
            check(db.save_scanned({ found }, error), "then a scan finds it and is saved");
        }

        DeviceDb db;
        std::string error;
        if (!require(db.open(file.utf8(), error), "the list reopens"))
            return;

        const auto list = load(db);
        if (!require(list.size() == 1, "one device, not an entry and a device"))
            return;

        const DeviceRecord& d = list[0];
        check(d.provenance == Provenance::Restored, "it comes back as a device that has answered");
        check(d.reached, "  and has been reached");
        check(d.product == ProductClassId::MiniPanelArm, "with the product it reported, over the one typed in");
        check(d.connection.host == "127.0.0.2", "and the address it answered from");
        check_eq((long)d.first_seen, 3000, "first seen when the scan found it");
        check_eq((long)d.last_seen, 3000, "  and last seen then");
        check(d.placement.name == "Boiler \"B\"", "its name kept");

        Database raw;
        if (require(raw.open(file.utf8(), error), "the file opens directly"))
            check_eq((long)single_int(raw, "SELECT added_by_hand FROM devices"), 1,
                     "and still records that it was added by hand");
    }

    void test_a_version_1_list_is_brought_up_to_date()
    {
        section("a list an older T5000 wrote is brought up to this build's schema, devices and all");

        TempFile file(L"v1");
        {
            Database raw;
            std::string error;
            if (!require(raw.open(file.utf8(), error), "a file is made"))
                return;

            // Version 1 as it shipped, spelled out here rather than taken
            // from device_db.cpp, so a change there cannot change what this
            // test calls version 1.
            check(raw.exec(
                      "CREATE TABLE devices ("
                      "  id               INTEGER PRIMARY KEY,"
                      "  kind             TEXT    NOT NULL CHECK (kind IN ('scanned', 'virtual')),"
                      "  serial           INTEGER NOT NULL CHECK (serial > 0 AND serial < 4294967295),"
                      "  product_class_id INTEGER NOT NULL,"
                      "  mini_type        INTEGER NOT NULL,"
                      "  firmware         INTEGER NOT NULL,"
                      "  modbus_id        INTEGER NOT NULL,"
                      "  parent_serial    INTEGER NOT NULL,"
                      "  object_instance  INTEGER NOT NULL,"
                      "  host             TEXT    NOT NULL,"
                      "  answered_from    TEXT    NOT NULL,"
                      "  reported_ip      TEXT    NOT NULL,"
                      "  bacnet_port      INTEGER NOT NULL,"
                      "  panel_name       TEXT    NOT NULL,"
                      "  name             TEXT    NOT NULL,"
                      "  building         TEXT    NOT NULL,"
                      "  floor            TEXT    NOT NULL,"
                      "  room             TEXT    NOT NULL,"
                      "  first_seen       INTEGER NOT NULL,"
                      "  last_seen        INTEGER NOT NULL,"
                      "  UNIQUE (kind, serial)"
                      ");"
                      "INSERT INTO devices (kind, serial, product_class_id, mini_type, firmware,"
                      " modbus_id, parent_serial, object_instance, host, answered_from, reported_ip,"
                      " bacnet_port, panel_name, name, building, floor, room, first_seen, last_seen)"
                      " VALUES ('scanned', 8001, 74, 7, 538, 12, 0, 4321, '127.0.0.2', '127.0.0.2',"
                      " '127.0.0.2', 47809, 'AHU 3', 'Boiler', 'North', '2', 'Plant', 1000, 2000);"
                      "PRAGMA user_version = 1;",
                      error),
                  "as version 1 of T5000 wrote it, with one device in it");
        }

        {
            DeviceDb db;
            std::string error;
            if (!require(db.open(file.utf8(), error), "this build opens it"))
                return;

            const auto list = load(db);
            if (require(list.size() == 1, "its device comes back"))
            {
                check(list[0].provenance == Provenance::Restored, "  as a device that has answered a scan");
                check(list[0].placement.name == "Boiler", "  with its name");
                check_eq((long)list[0].first_seen, 1000, "  and its history");
            }

            check(db.add_by_hand(typed_in(8101), error), "and a device can now be added by hand");
            check(db.save_scanned({ scanned(8002) }, error), "and a scanned one saved");
        }

        Database raw;
        std::string error;
        if (!require(raw.open(file.utf8(), error), "the file opens directly"))
            return;
        check_eq((long)single_int(raw, "PRAGMA user_version"), kSchemaVersion, "it is at this build's version");
        check_eq((long)single_int(raw, "SELECT added_by_hand FROM devices WHERE serial = 8001"), 0,
                 "the device from version 1 is not taken for one added by hand");
        check_eq((long)single_int(raw, "SELECT added_by_hand FROM devices WHERE serial = 8101"), 1,
                 "the one added since is");
        check_eq((long)single_int(raw, "SELECT added_by_hand FROM devices WHERE serial = 8002"), 0,
                 "and the one a scan saved since is not");
        check(!raw.exec("UPDATE devices SET added_by_hand = 2", error), "and the column takes only 0 or 1");
    }

    void test_a_new_list_is_made_the_same_way_an_old_one_is_upgraded()
    {
        section("a new list is made at version 1 and brought up, so there is one shape per version");

        TempFile file(L"fresh");
        {
            DeviceDb db;
            std::string error;
            if (!require(db.open(file.utf8(), error), "a new list is made"))
                return;
        }

        Database raw;
        std::string error;
        if (!require(raw.open(file.utf8(), error), "the file opens directly"))
            return;
        check_eq((long)single_int(raw, "PRAGMA user_version"), kSchemaVersion, "at this build's version");
        check_eq((long)single_int(raw, "SELECT count(*) FROM pragma_table_info('devices')"
                                       " WHERE name = 'added_by_hand' AND dflt_value = '0' AND \"notnull\" = 1"),
                 1, "with added_by_hand as the upgrade adds it");
        check_eq((long)single_int(raw, "SELECT count(*) FROM sqlite_master WHERE type = 'table' AND name = 'offline_points'"),
                 1, "and offline_points");
    }

    // ------------------------------------------------ offline configuration

    // 46 bytes, as an input is, told apart by one byte.
    std::vector<uint8_t> an_input(uint8_t mark)
    {
        std::vector<uint8_t> b(46, 0);
        b[0]  = 'I';
        b[1]  = 'N';
        b[2]  = mark;
        b[34] = 5;
        return b;
    }

    OfflinePoint a_change(int index, uint8_t base, uint8_t edited)
    {
        OfflinePoint p;
        p.index  = index;
        p.base   = an_input(base);
        p.edited = an_input(edited);
        return p;
    }

    std::vector<OfflinePoint> changes(DeviceDb& db, uint32_t serial)
    {
        std::vector<OfflinePoint> out;
        std::string error;
        check(db.load_offline_inputs(scanned_key(serial), out, error), "the offline changes load");
        return out;
    }

    // A BLOB literal of n bytes, for SQL written by hand.
    std::string zeros(size_t n)
    {
        return "X'" + std::string(n * 2, '0') + "'";
    }

    void test_bytes_are_stored_as_they_are()
    {
        section("bytes go in as a BLOB and come back exactly, zeros and quotes included");

        Database db;
        std::string error;
        if (!require(db.open(":memory:", error), "a database in memory opens") ||
            !require(db.exec("CREATE TABLE t (n INTEGER, b BLOB)", error), "with a table"))
            return;

        const uint8_t bytes[] = { 0, 1, 0, 0xFF, '\'', 0 };
        Statement i(db, "INSERT INTO t VALUES (1, ?1)");
        i.bind_blob(1, bytes, sizeof(bytes));
        check(i.step() == Statement::Step::Done, "six bytes are stored");

        Statement e(db, "INSERT INTO t VALUES (2, ?1)");
        e.bind_blob(1, nullptr, 0);
        check(e.step() == Statement::Step::Done, "and none");

        Statement q(db, "SELECT b, typeof(b), length(b) FROM t ORDER BY n");
        if (require(q.step() == Statement::Step::Row, "the first comes back"))
        {
            check(q.column_blob(0) == std::vector<uint8_t>(bytes, bytes + sizeof(bytes)), "  byte for byte");
            check(q.column_text(1) == "blob", "  as a BLOB");
            check_eq((long)q.column_int(2), 6, "  of six bytes, the zeros counted");
        }
        if (require(q.step() == Statement::Step::Row, "the second comes back"))
        {
            check(q.column_blob(0).empty(), "  with no bytes");
            check(q.column_text(1) == "blob", "  as an empty BLOB, not NULL");
        }
    }

    void test_offline_changes_keep_their_base()
    {
        section("an input changed offline keeps the base it started from, and goes when put back");

        DeviceDb db;
        std::string error;
        if (!open_memory(db) || !require(db.add_by_hand(typed_in(8301), error), "a device is added by hand"))
            return;

        check(changes(db, 8301).empty(), "it starts with no changes");

        check(db.save_offline_input(scanned_key(8301), a_change(3, 'a', 'b'), error), "input 4 is changed");
        auto saved = changes(db, 8301);
        if (require(saved.size() == 1, "  and saved"))
        {
            check_eq(saved[0].index, 3, "  as input 4");
            check(saved[0].base == an_input('a') && saved[0].edited == an_input('b'), "  with both sets of bytes");
        }

        check(db.save_offline_input(scanned_key(8301), a_change(3, 'x', 'c'), error), "it is changed again");
        saved = changes(db, 8301);
        check(saved.size() == 1 && saved[0].base == an_input('a') && saved[0].edited == an_input('c'),
              "  and keeps the base the first change started from, not the one given now");

        check(db.save_offline_input(scanned_key(8301), a_change(3, 'x', 'a'), error), "it is changed back to its base");
        check(changes(db, 8301).empty(), "  and its row is gone: nothing is left to write");

        check(db.save_offline_input(scanned_key(8301), a_change(5, 'x', 'x'), error), "an input 'changed' to what it was");
        check(changes(db, 8301).empty(), "  saves nothing");

        check(db.save_offline_input(scanned_key(8301), a_change(9, 'a', 'b'), error) &&
                  db.save_offline_input(scanned_key(8301), a_change(2, 'a', 'b'), error),
              "inputs 10 and 3 are changed");
        saved = changes(db, 8301);
        check(saved.size() == 2 && saved[0].index == 2 && saved[1].index == 9, "  and come back in index order");

        check(db.revert_offline_input(scanned_key(8301), 9, error), "input 10 is put back");
        saved = changes(db, 8301);
        check(saved.size() == 1 && saved[0].index == 2, "  and only input 3's change is left");
        check(db.revert_offline_input(scanned_key(8301), 40, error), "putting back an input with no changes is not an error");

        check(db.add_by_hand(typed_in(8302), error), "a second device is added");
        check(db.save_offline_input(scanned_key(8302), a_change(2, 'q', 'r'), error), "  and its input 3 changed");
        check(changes(db, 8301).size() == 1 && changes(db, 8301)[0].edited == an_input('b'),
              "each device's changes are its own");
    }

    void test_an_offline_change_must_be_one_input_of_a_saved_device()
    {
        section("an offline change is refused unless it is one input of a device in the list");

        DeviceDb db;
        std::string error;
        if (!open_memory(db) || !require(db.add_by_hand(typed_in(8351), error), "a device is added by hand"))
            return;

        check(db.references_held(), "the list holds each change to its device");

        check(!db.save_offline_input(scanned_key(8399), a_change(0, 'a', 'b'), error), "a serial not in the list is refused");
        check(error.find("not in the saved list") != std::string::npos, "  saying so");

        OfflinePoint short_one = a_change(0, 'a', 'b');
        short_one.edited.pop_back();
        check(!db.save_offline_input(scanned_key(8351), short_one, error), "45 bytes are refused");
        check(error.find("46 bytes") != std::string::npos, "  saying an input is 46");

        OfflinePoint long_base = a_change(0, 'a', 'b');
        long_base.base.push_back(0);
        check(!db.save_offline_input(scanned_key(8351), long_base, error), "a 47-byte base is refused");

        check(!db.save_offline_input(scanned_key(8351), a_change(255, 'a', 'b'), error), "index 255 is refused");
        check(error.find("not one a panel can have") != std::string::npos, "  saying so, before the table does");
        check(!db.save_offline_input(scanned_key(8351), a_change(-1, 'a', 'b'), error), "  and -1");
        check(db.save_offline_input(scanned_key(8351), a_change(254, 'a', 'b'), error), "index 254, the last a panel can have, is not");
        check(changes(db, 8351).size() == 1, "only that one was saved");
    }

    void test_the_offline_table_refuses_what_is_not_an_input()
    {
        section("the offline table itself refuses what is not one whole input of a saved device");

        TempFile file(L"offline");
        {
            DeviceDb db;
            std::string error;
            if (!require(db.open(file.utf8(), error), "a list is made") ||
                !require(db.add_by_hand(typed_in(8401), error), "with a device added by hand"))
                return;
        }

        Database raw;
        std::string error;
        if (!require(raw.open(file.utf8(), error), "the file opens directly") ||
            !require(raw.exec("PRAGMA foreign_keys = ON", error), "with its references held"))
            return;

        const std::string id = std::to_string(single_int(raw, "SELECT id FROM devices WHERE serial = 8401"));
        auto insert = [&](const std::string& idx, const std::string& kind, const std::string& base,
                          const std::string& edited, const std::string& device) {
            std::string e;
            return raw.exec(("INSERT INTO offline_points (device_id, kind, idx, base, edited) VALUES (" + device +
                             ", " + kind + ", " + idx + ", " + base + ", " + edited + ")").c_str(),
                            e);
        };

        // One that is right, so each refusal below is for its one difference.
        check(insert("0", "'input'", zeros(46), zeros(46), id), "one input of the device is taken");

        check(!insert("1", "'input'", zeros(45), zeros(46), id), "a 45-byte base is refused");
        check(!insert("1", "'input'", zeros(46), zeros(47), id), "a 47-byte edit is refused");
        check(!insert("1", "'output'", zeros(46), zeros(46), id), "a kind other than input is refused");
        check(!insert("255", "'input'", zeros(46), zeros(46), id), "index 255 is refused");
        check(!insert("-1", "'input'", zeros(46), zeros(46), id), "  and -1");
        check(!insert("1", "'input'", "'" + std::string(46, 'a') + "'", zeros(46), id),
              "text of 46 characters is refused as a base: a point is bytes");
        check(!insert("1", "'input'", zeros(46), "NULL", id), "no edit at all is refused");
        check(!insert("1", "'input'", zeros(46), zeros(46), "9999"), "a device not in the list is refused");
        check(!insert("0", "'input'", zeros(46), zeros(46), id), "a second row for the same input is refused");

        check(!raw.exec("DELETE FROM devices WHERE serial = 8401", error),
              "and the device cannot be deleted while a change points at it");
        check_eq((long)single_int(raw, "SELECT count(*) FROM offline_points"), 1, "only the right one was taken");
    }

    void test_scans_and_names_leave_offline_changes_alone()
    {
        section("a scan that finds the device, and a name given it, leave its offline changes alone");

        DeviceDb db;
        std::string error;
        if (!open_memory(db) || !require(db.add_by_hand(typed_in(8501), error), "a device is added by hand"))
            return;
        check(db.save_offline_input(scanned_key(8501), a_change(1, 'a', 'b'), error), "input 2 is changed offline");

        check(db.save_scanned({ scanned(8501, 3000) }, error), "a scan finds it");
        check(changes(db, 8501).size() == 1, "  and the change is still there");

        DeviceRecord named = scanned(8501, 3000);
        named.placement = a_placement();
        check(db.save_placement(named, error), "it is named");
        check(changes(db, 8501).size() == 1, "  and the change is still there");
    }

    void test_forgetting_takes_offline_changes()
    {
        section("forgetting a device takes its offline changes, and only its");

        DeviceDb db;
        std::string error;
        if (!open_memory(db) || !require(db.add_by_hand(typed_in(8601), error), "a device is added by hand") ||
            !require(db.add_by_hand(typed_in(8602), error), "and another"))
            return;
        check(db.save_offline_input(scanned_key(8601), a_change(0, 'a', 'b'), error) &&
                  db.save_offline_input(scanned_key(8602), a_change(0, 'a', 'c'), error),
              "each has an input changed");

        check(db.forget(scanned_key(8601), error), "the first is forgotten, its change pointing at it and all");
        check(changes(db, 8601).empty(), "  and its change is gone");
        check(changes(db, 8602).size() == 1, "  and the other's is not");

        check(db.add_by_hand(typed_in(8603), error), "a device added now");
        check(changes(db, 8603).empty(), "  has none of the forgotten one's");

        check(db.forget_all(error), "every device is forgotten");
        check(changes(db, 8602).empty(), "  with every change");
        check(db.add_by_hand(typed_in(8602), error) && changes(db, 8602).empty(),
              "and a serial added again starts with none");
    }

    void test_a_version_2_list_is_brought_up_to_date()
    {
        section("a list version 2 wrote is brought up to date, with its devices and room for offline changes");

        TempFile file(L"v2");
        {
            Database raw;
            std::string error;
            if (!require(raw.open(file.utf8(), error), "a file is made"))
                return;
            check(raw.exec("CREATE TABLE devices ("
                           "  id               INTEGER PRIMARY KEY,"
                           "  kind             TEXT    NOT NULL CHECK (kind IN ('scanned', 'virtual')),"
                           "  serial           INTEGER NOT NULL CHECK (serial > 0 AND serial < 4294967295),"
                           "  product_class_id INTEGER NOT NULL,"
                           "  mini_type        INTEGER NOT NULL,"
                           "  firmware         INTEGER NOT NULL,"
                           "  modbus_id        INTEGER NOT NULL,"
                           "  parent_serial    INTEGER NOT NULL,"
                           "  object_instance  INTEGER NOT NULL,"
                           "  host             TEXT    NOT NULL,"
                           "  answered_from    TEXT    NOT NULL,"
                           "  reported_ip      TEXT    NOT NULL,"
                           "  bacnet_port      INTEGER NOT NULL,"
                           "  panel_name       TEXT    NOT NULL,"
                           "  name             TEXT    NOT NULL,"
                           "  building         TEXT    NOT NULL,"
                           "  floor            TEXT    NOT NULL,"
                           "  room             TEXT    NOT NULL,"
                           "  first_seen       INTEGER NOT NULL,"
                           "  last_seen        INTEGER NOT NULL,"
                           "  UNIQUE (kind, serial)"
                           ");"
                           "ALTER TABLE devices ADD COLUMN added_by_hand INTEGER NOT NULL DEFAULT 0"
                           "  CHECK (added_by_hand IN (0, 1));"
                           "INSERT INTO devices (kind, serial, product_class_id, mini_type, firmware,"
                           " modbus_id, parent_serial, object_instance, host, answered_from, reported_ip,"
                           " bacnet_port, panel_name, name, building, floor, room, first_seen, last_seen,"
                           " added_by_hand)"
                           " VALUES ('scanned', 8201, 10, 11, 0, 0, 0, 0, '', '', '', 0, '', 'Lobby', '', '',"
                           " '', 0, 0, 1);"
                           "PRAGMA user_version = 2;",
                           error),
                  "as version 2 of T5000 wrote it, with a device added by hand in it");
        }

        {
            DeviceDb db;
            std::string error;
            if (!require(db.open(file.utf8(), error), "this build opens it"))
                return;

            const auto list = load(db);
            if (require(list.size() == 1, "its device comes back"))
            {
                check(list[0].provenance == Provenance::ManuallyAdded, "  still added by hand");
                check_eq(list[0].mini_type, 11, "  with the model chosen for it");
                check(list[0].placement.name == "Lobby", "  and its name");
            }
            check(db.save_offline_input(scanned_key(8201), a_change(0, 'a', 'b'), error), "and its inputs can now be changed offline");
        }

        Database raw;
        std::string error;
        if (!require(raw.open(file.utf8(), error), "the file opens directly"))
            return;
        check_eq((long)single_int(raw, "PRAGMA user_version"), kSchemaVersion, "it is at this build's version");
        check_eq((long)single_int(raw, "SELECT count(*) FROM offline_points"), 1, "with the change in its new table");
    }

    DeviceRecord on_port(uint32_t serial, const char* port, int baud, int slave_id)
    {
        DeviceRecord d = scanned(serial);
        d.connection.host.clear();
        d.answered_from.clear();
        d.reported_ip.clear();
        d.connection.transport       = Transport::ModbusRtu;
        d.connection.serial_port     = port;
        d.connection.baud            = baud;
        d.connection.modbus_slave_id = slave_id;
        d.provenance                 = Provenance::SerialScan;
        return d;
    }

    const DeviceRecord* with_serial(const std::vector<DeviceRecord>& list, uint32_t serial)
    {
        for (const DeviceRecord& d : list)
            if (d.serial_number == serial)
                return &d;
        return nullptr;
    }

    void test_a_serial_device_comes_back_on_its_port()
    {
        section("a device found on a serial port comes back on it, at its rate and id");

        DeviceDb db;
        if (!require(open_memory(db), "the list opens"))
            return;

        std::string error;
        check(db.save_scanned({ on_port(8301, "CNCA0", 76800, 7), on_port(8302, "COM4", 9600, 200), scanned(8303) },
                              error),
              "two serial devices and a network one are saved");

        const auto list = load(db);
        const DeviceRecord* a = with_serial(list, 8301);
        if (require(a != nullptr, "the one on CNCA0 comes back"))
        {
            check(a->connection.transport == Transport::ModbusRtu, "over Modbus RTU");
            check(a->connection.serial_port == "CNCA0", "on CNCA0");
            check_eq(a->connection.com_port, 0, "which is not COMn");
            check_eq(a->connection.baud, 76800, "at 76800");
            check_eq(a->connection.modbus_slave_id, 7, "on id 7");
            check(a->address_note == "CNCA0 id 7, 76800 baud", "and the list says so");
        }
        const DeviceRecord* b = with_serial(list, 8302);
        if (require(b != nullptr, "the one on COM4 comes back"))
        {
            check_eq(b->connection.com_port, 4, "as COM port 4");
            check_eq(b->connection.modbus_slave_id, 200, "on id 200");
        }
        const DeviceRecord* c = with_serial(list, 8303);
        if (require(c != nullptr, "the network one comes back"))
        {
            check(c->connection.transport == Transport::BacnetIp, "over BACnet/IP");
            check(c->connection.serial_port.empty(), "with no port");
            check(c->connection.host == "127.0.0.2", "at its address");
        }
    }

    void test_a_device_that_moves_is_saved_where_it_is()
    {
        section("a device found on the network, then on a port, then on the network, is saved each time where it is");

        DeviceDb db;
        if (!require(open_memory(db), "the list opens"))
            return;

        std::string error;
        check(db.save_scanned({ scanned(8401) }, error), "found on the network");
        check(db.save_scanned({ on_port(8401, "COM3", 19200, 9) }, error), "then on COM3");

        auto list = load(db);
        if (require(list.size() == 1, "one device"))
        {
            check(list[0].connection.transport == Transport::ModbusRtu, "on the port");
            check(list[0].connection.serial_port == "COM3", "COM3");
            check(list[0].connection.host.empty(), "with no network address");
        }

        check(db.save_scanned({ scanned(8401) }, error), "then on the network again");
        list = load(db);
        if (require(list.size() == 1, "still one device"))
        {
            check(list[0].connection.transport == Transport::BacnetIp, "on the network");
            check(list[0].connection.serial_port.empty(), "with the port gone");
            check(list[0].connection.host == "127.0.0.2", "at its address");
        }
    }

    void test_a_version_3_list_is_brought_up_to_date()
    {
        section("a list version 3 wrote is brought up to date, its devices on the network as they were");

        TempFile file(L"v3");
        {
            Database raw;
            std::string error;
            if (!require(raw.open(file.utf8(), error), "a file is made"))
                return;
            check(raw.exec("CREATE TABLE devices ("
                           "  id               INTEGER PRIMARY KEY,"
                           "  kind             TEXT    NOT NULL CHECK (kind IN ('scanned', 'virtual')),"
                           "  serial           INTEGER NOT NULL CHECK (serial > 0 AND serial < 4294967295),"
                           "  product_class_id INTEGER NOT NULL,"
                           "  mini_type        INTEGER NOT NULL,"
                           "  firmware         INTEGER NOT NULL,"
                           "  modbus_id        INTEGER NOT NULL,"
                           "  parent_serial    INTEGER NOT NULL,"
                           "  object_instance  INTEGER NOT NULL,"
                           "  host             TEXT    NOT NULL,"
                           "  answered_from    TEXT    NOT NULL,"
                           "  reported_ip      TEXT    NOT NULL,"
                           "  bacnet_port      INTEGER NOT NULL,"
                           "  panel_name       TEXT    NOT NULL,"
                           "  name             TEXT    NOT NULL,"
                           "  building         TEXT    NOT NULL,"
                           "  floor            TEXT    NOT NULL,"
                           "  room             TEXT    NOT NULL,"
                           "  first_seen       INTEGER NOT NULL,"
                           "  last_seen        INTEGER NOT NULL,"
                           "  UNIQUE (kind, serial)"
                           ");"
                           "ALTER TABLE devices ADD COLUMN added_by_hand INTEGER NOT NULL DEFAULT 0"
                           "  CHECK (added_by_hand IN (0, 1));"
                           "CREATE TABLE offline_points ("
                           "  device_id INTEGER NOT NULL REFERENCES devices (id),"
                           "  kind      TEXT    NOT NULL CHECK (kind IN ('input')),"
                           "  idx       INTEGER NOT NULL CHECK (idx BETWEEN 0 AND 254),"
                           "  base      BLOB    NOT NULL CHECK (typeof(base) = 'blob'),"
                           "  edited    BLOB    NOT NULL CHECK (typeof(edited) = 'blob'),"
                           "  CHECK (kind <> 'input' OR (length(base) = 46 AND length(edited) = 46)),"
                           "  PRIMARY KEY (device_id, kind, idx)"
                           ");"
                           "INSERT INTO devices (kind, serial, product_class_id, mini_type, firmware,"
                           " modbus_id, parent_serial, object_instance, host, answered_from, reported_ip,"
                           " bacnet_port, panel_name, name, building, floor, room, first_seen, last_seen,"
                           " added_by_hand)"
                           " VALUES ('scanned', 8501, 10, 7, 538, 12, 0, 4321, '10.0.0.5', '10.0.0.5', '10.0.0.5',"
                           " 47808, 'AHU 1', 'Roof', '', '', '', 100, 200, 0);"
                           "PRAGMA user_version = 3;",
                           error),
                  "as version 3 of T5000 wrote it, with a scanned device in it");
        }

        {
            DeviceDb db;
            std::string error;
            if (!require(db.open(file.utf8(), error), "this build opens it"))
                return;

            const auto list = load(db);
            if (require(list.size() == 1, "its device comes back"))
            {
                check(list[0].connection.transport == Transport::BacnetIp, "  over BACnet/IP, as every device then was");
                check(list[0].connection.host == "10.0.0.5", "  at its address");
                check(list[0].connection.serial_port.empty(), "  on no port");
                check(list[0].placement.name == "Roof", "  with its name");
            }

            check(db.save_scanned({ on_port(8501, "COM5", 38400, 12) }, error), "and it can be saved on a port now");
            const auto again = load(db);
            if (require(again.size() == 1, "still one device"))
                check(again[0].connection.serial_port == "COM5", "  on COM5");
        }

        Database raw;
        std::string error;
        if (!require(raw.open(file.utf8(), error), "the file opens directly"))
            return;
        check_eq((long)single_int(raw, "PRAGMA user_version"), kSchemaVersion, "it is at this build's version");
        check_eq((long)single_int(raw, "SELECT baud FROM devices"), 38400, "with the rate in its new column");
        check_eq((long)single_int(raw, "SELECT slave_id FROM devices"), 12, "and the id");
    }

    void test_the_table_refuses_a_transport_it_does_not_know()
    {
        section("the table itself refuses a transport it does not know, and an id out of range");

        TempFile file(L"transport");
        {
            DeviceDb db;
            std::string error;
            if (!require(db.open(file.utf8(), error), "a list is made"))
                return;
            check(db.save_scanned({ scanned(8601) }, error), "with a device in it");
        }

        Database raw;
        std::string error;
        if (!require(raw.open(file.utf8(), error), "the file opens directly"))
            return;
        check(!raw.exec("UPDATE devices SET transport = 'carrier-pigeon';", error), "an unknown transport is refused");
        check(!raw.exec("UPDATE devices SET slave_id = 256;", error), "an id over 255 is refused");
        check(!raw.exec("UPDATE devices SET slave_id = -1;", error), "and one under 0");
        check(raw.exec("UPDATE devices SET transport = 'modbus-rtu', serial_port = 'COM3', baud = 9600, slave_id = 3;",
                       error),
              "a serial device is taken");
        check_eq((long)single_int(raw, "SELECT count(*) FROM devices WHERE serial_port = '' AND baud = 0"), 0,
                 "and holds its port");
    }

    void test_the_default_path_is_beside_the_exe()
    {
        section("the list is kept beside the exe by default");

        const std::string path = default_db_path();
        const std::string tail = "\\T5000.db";
        check(path.size() > tail.size() &&
                  path.compare(path.size() - tail.size(), tail.size(), tail) == 0,
              "it is called T5000.db");
        check(path.find(':') != std::string::npos, "and the path is a full one");
    }
}

namespace
{
    DeviceRecord a_virtual(uint32_t serial)
    {
        DeviceRecord d;
        d.serial_number  = serial;
        d.product        = ProductClassId::MiniPanelArm;
        d.mini_type      = 5;   // a T3-BB
        d.provenance     = Provenance::Virtual;
        d.placement.name = "Spare";
        return d;
    }

    std::vector<OfflinePoint> changes_of(DeviceDb& db, const DeviceKey& key)
    {
        std::vector<OfflinePoint> out;
        std::string error;
        check(db.load_offline_inputs(key, out, error), "the offline changes load");
        return out;
    }

    void test_a_version_4_list_is_marked_as_one_that_may_hold_virtual_devices()
    {
        section("a list version 4 wrote is raised to 5, so a build before virtual devices refuses it");

        // Version 4 listed and forgot kind 'scanned' only.
        check(kSchemaVersion >= 5, "this build's version is past 4");

        TempFile file(L"v4");
        {
            DeviceDb db;
            std::string error;
            if (!require(db.open(file.utf8(), error), "a new list is made"))
                return;
            check(db.save_scanned({ scanned(8701) }, error), "with a device in it");
        }
        {
            // Version 5 changed no table, so a version 4 file is this one
            // with the old number.
            Database raw;
            std::string error;
            if (!require(raw.open(file.utf8(), error) && raw.exec("PRAGMA user_version = 4", error),
                         "as version 4 of T5000 left it"))
                return;
        }

        DeviceDb db;
        std::string error;
        if (!require(db.open(file.utf8(), error), "this build opens it"))
            return;
        const auto list = load(db);
        check(list.size() == 1 && list[0].serial_number == 8701, "its device comes back");
        db.close();

        Database raw;
        if (require(raw.open(file.utf8(), error), "the file opens directly"))
            check_eq((long)single_int(raw, "PRAGMA user_version"), kSchemaVersion, "it is at this build's version");
    }

    void test_a_virtual_device_comes_back_as_virtual()
    {
        section("a virtual device is saved as kind 'virtual', and comes back as one");

        TempFile file(L"virtual");
        DeviceDb db;
        std::string error;
        if (!require(db.open(file.utf8(), error), "a new list is made"))
            return;

        const uint32_t v = kFirstVirtualSerial;
        check(db.add_virtual(a_virtual(v), error), "a virtual device is saved");

        std::vector<DeviceRecord> out;
        check(db.load(out, error), "the list loads");
        if (require(out.size() == 1, "  with it in it"))
        {
            check(out[0].is_virtual(), "  as a virtual device");
            check(!out[0].reached, "  never reached");
            check_eq(out[0].mini_type, 5, "  with its model");
            check(out[0].placement.name == "Spare", "  and its name");
            check(out[0].connection.host.empty() && out[0].address_note.empty(), "  and no address");
        }

        {
            Database raw;
            if (require(raw.open(file.utf8(), error), "the file opens directly"))
            {
                Statement kind(raw, "SELECT kind FROM devices WHERE serial = ?1");
                kind.bind(1, (int64_t)v);
                check(kind.step() == Statement::Step::Row && kind.column_text(0) == "virtual",
                      "  in a row of kind 'virtual'");
            }
        }

        check(!db.add_virtual(a_virtual(v), error), "a second virtual device with that serial is refused");
        check(!db.add_virtual(scanned(kFirstVirtualSerial + 1), error), "a record that is not virtual is refused");
        check(!db.add_virtual(a_virtual(12345), error), "a serial outside the range is refused");
        check(error.find("12345") != std::string::npos, "  naming it");
        check(!db.add_virtual(a_virtual(0xFFFFFFFFu), error), "0xFFFFFFFF, no serial, is refused");
    }

    void test_a_virtual_and_a_real_device_with_one_serial_are_two_rows()
    {
        section("a real device answering with a virtual device's serial is another row, with its own changes");

        DeviceDb db;
        if (!open_memory(db))
            return;

        std::string error;
        const uint32_t s = kFirstVirtualSerial;
        check(db.add_virtual(a_virtual(s), error), "a virtual device is saved");
        check(db.save_offline_input(virtual_key(s), a_change(1, 'a', 'v'), error), "  and its input 2 changed");

        check(db.save_scanned({ scanned(s) }, error), "a scan saves a real device with the same serial");
        check(db.save_offline_input(scanned_key(s), a_change(3, 'a', 'r'), error), "  and its input 4 changed");

        std::vector<DeviceRecord> out;
        check(db.load(out, error) && out.size() == 2, "the list holds both");
        if (out.size() == 2)
        {
            check(out[0].is_virtual() && out[0].placement.name == "Spare", "  the virtual one as it was");
            check(out[1].provenance == Provenance::Restored && out[1].connection.host == "127.0.0.2",
                  "  and the real one as the scan saw it");
        }

        auto v = changes_of(db, virtual_key(s));
        auto r = changes_of(db, scanned_key(s));
        check(v.size() == 1 && v[0].index == 1, "each keeps its own change: the virtual device its input 2");
        check(r.size() == 1 && r[0].index == 3, "  and the real one its input 4");

        check(db.revert_offline_input(virtual_key(s), 1, error), "the virtual device's change is undone");
        check(changes_of(db, virtual_key(s)).empty() && changes_of(db, scanned_key(s)).size() == 1,
              "  leaving the real one's");

        std::vector<OfflinePoint> one = { a_change(7, 'a', 'z') };
        check(db.replace_offline_inputs(virtual_key(s), one, error), "an import replaces the virtual device's");
        check(changes_of(db, scanned_key(s)).size() == 1 && changes_of(db, scanned_key(s))[0].index == 3,
              "  and not the real one's");

        DeviceRecord renamed = a_virtual(s);
        renamed.placement.name = "Renamed";
        renamed.mini_type = 6;
        check(db.save_placement(renamed, error), "the virtual device is renamed and its model changed");
        check(db.load(out, error) && out.size() == 2, "  which adds no row");
        if (out.size() == 2)
        {
            check(out[0].placement.name == "Renamed" && out[0].mini_type == 6, "  and changes the virtual row");
            check(out[1].placement.name.empty() && out[1].mini_type == 7, "  and not the real one");
        }

        check(db.forget(scanned_key(s), error), "the real one is forgotten");
        check(db.load(out, error) && out.size() == 1 && out[0].is_virtual(), "  leaving the virtual one");
        check(changes_of(db, virtual_key(s)).size() == 1, "  with its change");
        check(changes_of(db, scanned_key(s)).empty(), "  and the real one's change gone with it");

        check(db.forget(virtual_key(s), error), "the virtual one is forgotten");
        check(db.load(out, error) && out.empty(), "  leaving nothing");
        check(changes_of(db, virtual_key(s)).empty(), "  and no change");
    }

    void test_forgetting_all_takes_virtual_devices()
    {
        section("forgetting every device takes the virtual ones and their changes too");

        TempFile file(L"forgetall");
        DeviceDb db;
        std::string error;
        if (!require(db.open(file.utf8(), error), "a new list is made"))
            return;

        check(db.add_virtual(a_virtual(kFirstVirtualSerial), error) && db.save_scanned({ scanned(8801) }, error),
              "a virtual device and a real one are saved");
        check(db.save_offline_input(virtual_key(kFirstVirtualSerial), a_change(0, 'a', 'b'), error),
              "  and the virtual one's input 1 changed");

        check(db.forget_all(error), "every device is forgotten");
        std::vector<DeviceRecord> out;
        check(db.load(out, error) && out.empty(), "  none is left");

        Database raw;
        if (require(raw.open(file.utf8(), error), "the file opens directly"))
        {
            Statement points(raw, "SELECT count(*) FROM offline_points");
            check(points.step() == Statement::Step::Row && points.column_int(0) == 0, "  and no change is left");
        }
    }
}

int run_device_db_tests()
{
    test_text_is_bound_not_spliced();
    test_a_broken_statement_says_why();
    test_a_saved_device_comes_back_restored();
    test_a_rescan_keeps_what_the_operator_typed();
    test_last_seen_does_not_go_backwards();
    test_a_device_with_no_serial_is_never_saved();
    test_the_table_itself_refuses_an_unkeyed_row();
    test_a_placement_saves_a_device_not_yet_saved();
    test_forgetting();
    test_the_list_survives_closing();
    test_a_newer_file_is_left_alone();
    test_someone_elses_database_is_left_alone();
    test_a_foreign_database_claiming_our_version_is_left_alone();
    test_a_file_that_is_not_sqlite_is_left_alone();
    test_a_device_added_by_hand_comes_back_as_added_by_hand();
    test_adding_by_hand_never_overwrites_a_saved_device();
    test_a_scan_that_finds_a_device_added_by_hand();
    test_a_version_1_list_is_brought_up_to_date();
    test_a_new_list_is_made_the_same_way_an_old_one_is_upgraded();
    test_bytes_are_stored_as_they_are();
    test_offline_changes_keep_their_base();
    test_an_offline_change_must_be_one_input_of_a_saved_device();
    test_the_offline_table_refuses_what_is_not_an_input();
    test_scans_and_names_leave_offline_changes_alone();
    test_forgetting_takes_offline_changes();
    test_a_version_2_list_is_brought_up_to_date();
    test_a_serial_device_comes_back_on_its_port();
    test_a_device_that_moves_is_saved_where_it_is();
    test_a_version_3_list_is_brought_up_to_date();
    test_the_table_refuses_a_transport_it_does_not_know();
    test_the_default_path_is_beside_the_exe();
    test_a_version_4_list_is_marked_as_one_that_may_hold_virtual_devices();
    test_a_virtual_device_comes_back_as_virtual();
    test_a_virtual_and_a_real_device_with_one_serial_are_two_rows();
    test_forgetting_all_takes_virtual_devices();
    return 0;
}
