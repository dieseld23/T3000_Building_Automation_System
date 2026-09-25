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

        check(db.forget(8002, error), "one is forgotten");
        auto list = load(db);
        check_eq((long)list.size(), 2, "two remain");
        for (const auto& d : list)
            check(d.serial_number != 8002, "and the forgotten one is not among them");

        check(db.forget(9999, error), "forgetting a device that was never saved is not an error");

        check(db.forget_all_scanned(error), "the rest are forgotten");
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
    test_the_default_path_is_beside_the_exe();
    return 0;
}
