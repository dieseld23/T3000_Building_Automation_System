#include "device_db.h"

#include <windows.h>

#include "../serial/ports.h"
#include "../wire/decode.h"

namespace t5000::store
{
    namespace
    {
        using namespace t5000::device;

        // Version 1.
        //
        // kind is 'scanned' for a real device: one a scan can find, keyed on
        // the serial it reports. Since version 2 that includes a device added
        // by hand before a scan has found it. 'virtual' is allowed now for the
        // virtual devices that come next, because SQLite cannot change a CHECK
        // constraint without rebuilding the table.
        //
        // The serial CHECK is is_uninitialised_serial, in SQL: 0 and
        // 0xFFFFFFFF are never keys.
        const char* const kCreateV1 =
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
            ");";

        // Version 2: whether the operator added the device by hand.
        //
        // A column rather than a third kind. A device added by hand and the
        // device a scan later finds with its serial are one device, so they
        // must be one row, which UNIQUE (kind, serial) holds only while both
        // are the same kind. And SQLite adds a column in place, where a new
        // kind would mean rebuilding the table to change its CHECK.
        //
        // It stays set once the device has been found. It says how the device
        // came to be listed; whether it has answered a scan since is
        // last_seen's to say.
        const char* const kUpgradeToV2 =
            "ALTER TABLE devices ADD COLUMN added_by_hand INTEGER NOT NULL DEFAULT 0"
            "  CHECK (added_by_hand IN (0, 1));";

        // Version 3: points changed on a device before it can be reached.
        //
        // Keyed on the device's row, not its serial, so the changes belong to
        // that entry: when a scan finds a device added by hand it updates the
        // same row (write()), and the changes stay with it. Forgetting the
        // device deletes them first; the reference refuses the other order,
        // since open() turns foreign keys on, so no change can outlive its
        // device and be handed to the next one given that row's id.
        //
        // kind is 'input' only for now. The pages for outputs and variables
        // add theirs, and a CHECK cannot be changed in place, so the next one
        // rebuilds this table.
        //
        // idx is one byte on the wire, so 0-254. A point is its ud_str.h
        // struct, whole: 46 bytes for Str_in_point.
        const char* const kUpgradeToV3 =
            "CREATE TABLE offline_points ("
            "  device_id INTEGER NOT NULL REFERENCES devices (id),"
            "  kind      TEXT    NOT NULL CHECK (kind IN ('input')),"
            "  idx       INTEGER NOT NULL CHECK (idx BETWEEN 0 AND 254),"
            "  base      BLOB    NOT NULL CHECK (typeof(base) = 'blob'),"
            "  edited    BLOB    NOT NULL CHECK (typeof(edited) = 'blob'),"
            "  CHECK (kind <> 'input' OR (length(base) = 46 AND length(edited) = 46)),"
            "  PRIMARY KEY (device_id, kind, idx)"
            ");";

        // Version 4: how a device is reached. One a serial scan found is on a
        // port, at a rate, on an id, and none of the columns above can hold
        // those: until now it came back from the list as a BACnet/IP device
        // with no address. A row from before this is BACnet/IP, which is what
        // every device saved then was.
        //
        // transport allows all four of device::Transport's names now, since a
        // CHECK cannot be widened later without rebuilding the table.
        // serial_port, baud and slave_id are set for a serial transport only,
        // and are '' and 0 otherwise.
        const char* const kUpgradeToV4 =
            "ALTER TABLE devices ADD COLUMN transport TEXT NOT NULL DEFAULT 'bacnet-ip'"
            "  CHECK (transport IN ('bacnet-ip', 'bacnet-mstp', 'modbus-tcp', 'modbus-rtu'));"
            "ALTER TABLE devices ADD COLUMN serial_port TEXT NOT NULL DEFAULT '';"
            "ALTER TABLE devices ADD COLUMN baud INTEGER NOT NULL DEFAULT 0;"
            "ALTER TABLE devices ADD COLUMN slave_id INTEGER NOT NULL DEFAULT 0"
            "  CHECK (slave_id BETWEEN 0 AND 255);";

        // The serial columns of a device, '' and 0 unless it is reached over
        // a serial port.
        struct SerialColumns
        {
            std::string port;
            int64_t     baud     = 0;
            int64_t     slave_id = 0;
        };

        SerialColumns serial_columns(const DeviceRecord& d)
        {
            SerialColumns c;
            if (transport_is_serial(d.connection.transport) && !d.connection.serial_port.empty())
            {
                c.port     = d.connection.serial_port;
                c.baud     = d.connection.baud;
                c.slave_id = d.connection.modbus_slave_id;
            }
            return c;
        }

        static_assert(t5000::wire::kInputPointWireSize == 46,
                      "offline_points checks an input's length as 46; change it with the struct");

        bool read_int(Database& db, const char* sql, int64_t& out, std::string& error)
        {
            Statement q(db, sql);
            if (q.step() != Statement::Step::Row)
            {
                error = q.error();
                return false;
            }
            out = q.column_int(0);
            return true;
        }

        std::string utf8_from_wide(const wchar_t* w)
        {
            const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
            if (n <= 1)
                return std::string();
            std::string out((size_t)n, '\0');
            WideCharToMultiByte(CP_UTF8, 0, w, -1, &out[0], n, nullptr, nullptr);
            out.resize((size_t)n - 1);
            return out;
        }
    }

    std::string default_db_path()
    {
        // The wide API, so a folder such as C:\Users\José\Desktop reaches
        // SQLite intact. SQLite takes UTF-8; the ANSI API would hand it the
        // code page's bytes instead.
        wchar_t exe[MAX_PATH] = {};
        const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
        if (n == 0 || n >= MAX_PATH)
            return "T5000.db";

        std::wstring path(exe, n);
        const size_t slash = path.find_last_of(L"\\/");
        if (slash != std::wstring::npos)
            path.resize(slash + 1);
        else
            path.clear();

        return utf8_from_wide((path + L"T5000.db").c_str());
    }

    bool DeviceDb::open(const std::string& path, std::string& error)
    {
        close();

        if (!m_db.open(path, error))
            return false;

        int64_t version = 0;
        if (!read_int(m_db, "PRAGMA user_version", version, error))
        {
            // What SQLite says about a file that is not one of its own.
            close();
            return false;
        }

        // So that offline_points' reference to devices is held, not only
        // declared. Per connection, and outside a transaction, so here.
        if (!m_db.exec("PRAGMA foreign_keys = ON", error))
        {
            close();
            return false;
        }

        if (version > kSchemaVersion)
        {
            error = "it was written by a newer T5000 (schema " + std::to_string(version) +
                    "; this build knows " + std::to_string(kSchemaVersion) +
                    "), so this build does not use it";
            close();
            return false;
        }

        if (version == 0)
        {
            // A new file has no tables. One that has tables and no version
            // is someone else's database, and creating ours inside it would
            // be writing into a file nobody asked us to touch.
            int64_t tables = 0;
            if (!read_int(m_db, "SELECT count(*) FROM sqlite_master", tables, error))
            {
                close();
                return false;
            }
            if (tables != 0)
            {
                error = "it is a SQLite database, but not a T5000 device list";
                close();
                return false;
            }
        }
        else
        {
            // Any program can set user_version, and 1 is an obvious choice.
            // A file that says 1 and has no devices table is not ours either.
            int64_t ours = 0;
            if (!read_int(m_db, "SELECT count(*) FROM sqlite_master WHERE type = 'table' AND name = 'devices'",
                          ours, error))
            {
                close();
                return false;
            }
            if (ours != 1)
            {
                error = "it is a SQLite database, but not a T5000 device list";
                close();
                return false;
            }
        }

        if (version < kSchemaVersion)
        {
            // One path for a new file and an old one. A new file is made at
            // version 1 and brought up from there, so every new list goes
            // through the same steps as one an older T5000 has been keeping
            // for months, and there is one shape for each version, not two.
            //
            // In one transaction: a step that fails leaves the file exactly
            // as it was, at the version it was.
            const std::string set_version = "PRAGMA user_version = " + std::to_string(kSchemaVersion);

            Transaction t(m_db);
            bool ok = t.began();
            if (ok && version < 1)
                ok = m_db.exec(kCreateV1, error);
            if (ok && version < 2)
                ok = m_db.exec(kUpgradeToV2, error);
            if (ok && version < 3)
                ok = m_db.exec(kUpgradeToV3, error);
            if (ok && version < 4)
                ok = m_db.exec(kUpgradeToV4, error);
            if (ok)
                ok = m_db.exec(set_version.c_str(), error);
            if (ok)
                ok = t.commit();

            if (!ok)
            {
                if (error.empty())
                    error = t.error();
                if (version != 0)
                    error = "it could not be brought up to this build's version (" + error + ")";
                close();
                return false;
            }
        }

        m_path = path;
        return true;
    }

    void DeviceDb::close()
    {
        m_db.close();
        m_path.clear();
    }

    bool DeviceDb::load(std::vector<DeviceRecord>& out, std::string& error)
    {
        out.clear();

        Statement q(m_db,
                    "SELECT serial, product_class_id, mini_type, firmware, modbus_id,"
                    "       parent_serial, object_instance, host, answered_from, reported_ip,"
                    "       bacnet_port, panel_name, name, building, floor, room,"
                    "       first_seen, last_seen, added_by_hand, transport, serial_port, baud, slave_id"
                    "  FROM devices WHERE kind = 'scanned' ORDER BY id");

        for (;;)
        {
            const Statement::Step s = q.step();
            if (s == Statement::Step::Done)
                return true;
            if (s == Statement::Step::Error)
            {
                error = q.error();
                out.clear();
                return false;
            }

            DeviceRecord d;
            d.serial_number      = (uint32_t)q.column_int(0);
            d.product            = static_cast<ProductClassId>((uint8_t)q.column_int(1));
            d.mini_type          = (int)q.column_int(2);
            d.firmware           = (int)q.column_int(3);
            d.modbus_id_reported = (int)q.column_int(4);
            d.parent_serial      = (uint32_t)q.column_int(5);

            // Reached the way the scan that saved it reached it.
            d.connection.transport       = Transport::BacnetIp;
            d.connection.device_instance = (int)q.column_int(6);
            d.connection.host            = q.column_text(7);
            d.connection.modbus_slave_id = d.modbus_id_reported;
            d.answered_from              = q.column_text(8);
            d.reported_ip                = q.column_text(9);
            if (q.column_int(10) != 0)
                d.connection.udp_port = (int)q.column_int(10);
            d.address_note = d.connection.host;
            d.panel_name   = q.column_text(11);

            // A device found on a serial port comes back on it. A transport
            // this build does not know, or a serial one with no port, is left
            // as BACnet/IP with no address, as before version 4: the reads
            // refuse it, rather than guess where it is.
            Transport transport = Transport::BacnetIp;
            const std::string port = q.column_text(20);
            if (transport_from_name(q.column_text(19), transport) && transport_is_serial(transport) &&
                !port.empty())
            {
                d.connection.transport   = transport;
                d.connection.serial_port = port;
                d.connection.com_port    = serial::com_number(port);
                d.connection.baud        = (int)q.column_int(21);
                if (q.column_int(22) != 0)
                    d.connection.modbus_slave_id = (int)q.column_int(22);
                d.address_note = port + " id " + std::to_string(d.connection.modbus_slave_id) + ", " +
                                 std::to_string(d.connection.baud) + " baud";
            }

            d.placement.name     = q.column_text(12);
            d.placement.building = q.column_text(13);
            d.placement.floor    = q.column_text(14);
            d.placement.room     = q.column_text(15);

            d.first_seen = q.column_int(16);
            d.last_seen  = q.column_int(17);

            // A device added by hand that has not been found has nothing a
            // scan or Find learned: no address, no firmware, no panel name, only what
            // the operator typed. It comes back as it was added, and as never
            // reached, so nothing takes the product the operator picked for
            // one the device reported.
            //
            // Decided on last_seen as well as the flag, which stays set once
            // the device is found. A device added by hand that has since
            // been found, by a scan or by Find, comes back like any other
            // saved device.
            const bool added_by_hand = q.column_int(18) != 0;
            if (added_by_hand && d.last_seen == 0)
            {
                d.provenance = Provenance::ManuallyAdded;
                d.reached    = false;
            }
            else
            {
                // Saved because it answered a scan or Find, so it has been
                // reached - in an earlier session.
                d.provenance = Provenance::Restored;
                d.reached    = true;
            }

            // answered_scan stays 0 either way: it has not answered a scan in
            // this session.
            d.observation_complete = false;

            out.push_back(d);
        }
    }

    bool DeviceDb::write(const DeviceRecord& d, bool with_placement, std::string& error)
    {
        Statement find(m_db, "SELECT id FROM devices WHERE kind = 'scanned' AND serial = ?1");
        find.bind(1, (int64_t)d.serial_number);

        const Statement::Step found = find.step();
        if (found == Statement::Step::Error)
        {
            error = find.error();
            return false;
        }

        const int64_t first = d.first_seen != 0 ? d.first_seen : d.last_seen;

        if (found == Statement::Step::Row)
        {
            const int64_t id = find.column_int(0);

            // The observed fields, as the registry holds them after the merge.
            // Not name, building, floor or room: those are the operator's, and
            // an observation says nothing about them.
            //
            // first_seen is history, and set only while it is 0, which is how
            // a device added by hand is saved. The first scan to find it
            // gives it one; no later scan moves it.
            Statement u(m_db,
                        "UPDATE devices SET product_class_id = ?2, mini_type = ?3, firmware = ?4,"
                        "       modbus_id = ?5, parent_serial = ?6, object_instance = ?7,"
                        "       host = ?8, answered_from = ?9, reported_ip = ?10,"
                        "       bacnet_port = ?11, panel_name = ?12,"
                        "       last_seen = max(last_seen, ?13),"
                        "       first_seen = CASE WHEN first_seen = 0 THEN ?14 ELSE first_seen END,"
                        "       transport = ?15, serial_port = ?16, baud = ?17, slave_id = ?18"
                        " WHERE id = ?1");
            u.bind(1, id);
            u.bind(2, (int64_t)static_cast<uint8_t>(d.product));
            u.bind(3, (int64_t)d.mini_type);
            u.bind(4, (int64_t)d.firmware);
            u.bind(5, (int64_t)d.modbus_id_reported);
            u.bind(6, (int64_t)d.parent_serial);
            u.bind(7, (int64_t)d.connection.device_instance);
            u.bind(8, d.connection.host);
            u.bind(9, d.answered_from);
            u.bind(10, d.reported_ip);
            u.bind(11, (int64_t)d.connection.udp_port);
            u.bind(12, d.panel_name);
            u.bind(13, d.last_seen);
            u.bind(14, first);
            const SerialColumns sc = serial_columns(d);
            u.bind(15, std::string(transport_name(d.connection.transport)));
            u.bind(16, sc.port);
            u.bind(17, sc.baud);
            u.bind(18, sc.slave_id);
            if (u.step() != Statement::Step::Done)
            {
                error = u.error();
                return false;
            }

            if (with_placement)
            {
                Statement p(m_db,
                            "UPDATE devices SET name = ?2, building = ?3, floor = ?4, room = ?5"
                            " WHERE id = ?1");
                p.bind(1, id);
                p.bind(2, d.placement.name);
                p.bind(3, d.placement.building);
                p.bind(4, d.placement.floor);
                p.bind(5, d.placement.room);
                if (p.step() != Statement::Step::Done)
                {
                    error = p.error();
                    return false;
                }
            }
            return true;
        }

        // New. The whole record, placement included: a device the registry
        // already has a name for - because its row was lost, say - keeps it.
        return insert(d, /*added_by_hand*/ false, error);
    }

    bool DeviceDb::insert(const DeviceRecord& d, bool added_by_hand, std::string& error)
    {
        const int64_t first = d.first_seen != 0 ? d.first_seen : d.last_seen;

        Statement i(m_db,
                    "INSERT INTO devices (kind, serial, product_class_id, mini_type, firmware,"
                    "       modbus_id, parent_serial, object_instance, host, answered_from,"
                    "       reported_ip, bacnet_port, panel_name, name, building, floor, room,"
                    "       first_seen, last_seen, added_by_hand, transport, serial_port, baud, slave_id)"
                    " VALUES ('scanned', ?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12,"
                    "         ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20, ?21, ?22, ?23)");
        i.bind(1, (int64_t)d.serial_number);
        i.bind(2, (int64_t)static_cast<uint8_t>(d.product));
        i.bind(3, (int64_t)d.mini_type);
        i.bind(4, (int64_t)d.firmware);
        i.bind(5, (int64_t)d.modbus_id_reported);
        i.bind(6, (int64_t)d.parent_serial);
        i.bind(7, (int64_t)d.connection.device_instance);
        i.bind(8, d.connection.host);
        i.bind(9, d.answered_from);
        i.bind(10, d.reported_ip);
        i.bind(11, (int64_t)d.connection.udp_port);
        i.bind(12, d.panel_name);
        i.bind(13, d.placement.name);
        i.bind(14, d.placement.building);
        i.bind(15, d.placement.floor);
        i.bind(16, d.placement.room);
        i.bind(17, first);
        i.bind(18, d.last_seen);
        i.bind(19, (int64_t)(added_by_hand ? 1 : 0));
        const SerialColumns sc = serial_columns(d);
        i.bind(20, std::string(transport_name(d.connection.transport)));
        i.bind(21, sc.port);
        i.bind(22, sc.baud);
        i.bind(23, sc.slave_id);
        if (i.step() != Statement::Step::Done)
        {
            error = i.error();
            return false;
        }
        return true;
    }

    bool DeviceDb::add_by_hand(const DeviceRecord& device, std::string& error)
    {
        if (!device.has_stable_identity())
        {
            error = "a device with no serial number cannot be saved";
            return false;
        }

        // Asked first, for a reason worth reading. The table's UNIQUE would
        // refuse the insert below anyway, in SQLite's words.
        Statement find(m_db, "SELECT added_by_hand FROM devices WHERE kind = 'scanned' AND serial = ?1");
        find.bind(1, (int64_t)device.serial_number);

        const Statement::Step found = find.step();
        if (found == Statement::Step::Error)
        {
            error = find.error();
            return false;
        }
        if (found == Statement::Step::Row)
        {
            error = "serial " + std::to_string(device.serial_number) + " is already in the saved list" +
                    (find.column_int(0) != 0 ? ", added by hand" : "");
            return false;
        }

        return insert(device, /*added_by_hand*/ true, error);
    }

    bool DeviceDb::save_scanned(const std::vector<DeviceRecord>& devices, std::string& error)
    {
        Transaction t(m_db);
        if (!t.began())
        {
            error = t.error();
            return false;
        }

        for (const auto& d : devices)
        {
            if (!d.has_stable_identity())
                continue;
            if (!write(d, /*with_placement*/ false, error))
                return false;   // rolled back by t
        }

        if (!t.commit())
        {
            error = t.error();
            return false;
        }
        return true;
    }

    bool DeviceDb::save_placement(const DeviceRecord& device, std::string& error)
    {
        if (!device.has_stable_identity())
        {
            error = "a device with no serial number cannot be saved";
            return false;
        }

        Transaction t(m_db);
        if (!t.began())
        {
            error = t.error();
            return false;
        }
        if (!write(device, /*with_placement*/ true, error))
            return false;
        if (!t.commit())
        {
            error = t.error();
            return false;
        }
        return true;
    }

    bool DeviceDb::forget(uint32_t serial, std::string& error)
    {
        Transaction t(m_db);
        if (!t.began())
        {
            error = t.error();
            return false;
        }

        // The changes first: the reference refuses the device while they
        // point at it.
        Statement points(m_db,
                         "DELETE FROM offline_points WHERE device_id IN"
                         " (SELECT id FROM devices WHERE kind = 'scanned' AND serial = ?1)");
        points.bind(1, (int64_t)serial);
        if (points.step() != Statement::Step::Done)
        {
            error = points.error();
            return false;
        }

        Statement q(m_db, "DELETE FROM devices WHERE kind = 'scanned' AND serial = ?1");
        q.bind(1, (int64_t)serial);
        if (q.step() != Statement::Step::Done)
        {
            error = q.error();
            return false;
        }

        if (!t.commit())
        {
            error = t.error();
            return false;
        }
        return true;
    }

    bool DeviceDb::forget_all_scanned(std::string& error)
    {
        Transaction t(m_db);
        if (!t.began())
        {
            error = t.error();
            return false;
        }

        Statement points(m_db,
                         "DELETE FROM offline_points WHERE device_id IN"
                         " (SELECT id FROM devices WHERE kind = 'scanned')");
        if (points.step() != Statement::Step::Done)
        {
            error = points.error();
            return false;
        }

        Statement q(m_db, "DELETE FROM devices WHERE kind = 'scanned'");
        if (q.step() != Statement::Step::Done)
        {
            error = q.error();
            return false;
        }

        if (!t.commit())
        {
            error = t.error();
            return false;
        }
        return true;
    }

    bool DeviceDb::load_offline_inputs(uint32_t serial, std::vector<OfflinePoint>& out, std::string& error)
    {
        out.clear();

        Statement q(m_db,
                    "SELECT idx, base, edited FROM offline_points"
                    " WHERE kind = 'input' AND device_id ="
                    "       (SELECT id FROM devices WHERE kind = 'scanned' AND serial = ?1)"
                    " ORDER BY idx");
        q.bind(1, (int64_t)serial);

        for (;;)
        {
            const Statement::Step s = q.step();
            if (s == Statement::Step::Done)
                return true;
            if (s == Statement::Step::Error)
            {
                error = q.error();
                out.clear();
                return false;
            }

            OfflinePoint p;
            p.index  = (int)q.column_int(0);
            p.base   = q.column_blob(1);
            p.edited = q.column_blob(2);
            out.push_back(p);
        }
    }

    bool DeviceDb::save_offline_input(uint32_t serial, const OfflinePoint& point, std::string& error)
    {
        // Said here in words; the table's CHECKs would refuse both in
        // SQLite's.
        if (point.base.size() != wire::kInputPointWireSize || point.edited.size() != wire::kInputPointWireSize)
        {
            error = "an input is " + std::to_string(wire::kInputPointWireSize) + " bytes";
            return false;
        }
        if (point.index < 0 || point.index > 254)
        {
            error = "input " + std::to_string(point.index + 1) + " is not one a panel can have";
            return false;
        }

        Transaction t(m_db);
        if (!t.began())
        {
            error = t.error();
            return false;
        }

        Statement device(m_db, "SELECT id FROM devices WHERE kind = 'scanned' AND serial = ?1");
        device.bind(1, (int64_t)serial);
        const Statement::Step found = device.step();
        if (found == Statement::Step::Error)
        {
            error = device.error();
            return false;
        }
        if (found != Statement::Step::Row)
        {
            error = "serial " + std::to_string(serial) + " is not in the saved list";
            return false;
        }
        const int64_t id = device.column_int(0);

        Statement existing(m_db, "SELECT base FROM offline_points WHERE device_id = ?1 AND kind = 'input' AND idx = ?2");
        existing.bind(1, id);
        existing.bind(2, (int64_t)point.index);
        const Statement::Step had = existing.step();
        if (had == Statement::Step::Error)
        {
            error = existing.error();
            return false;
        }

        // The base already saved wins over the one given: it is what the
        // first change started from.
        const std::vector<uint8_t> base = had == Statement::Step::Row ? existing.column_blob(0) : point.base;

        const char* sql = nullptr;
        if (point.edited == base)
            sql = "DELETE FROM offline_points WHERE device_id = ?1 AND kind = 'input' AND idx = ?2";
        else if (had == Statement::Step::Row)
            sql = "UPDATE offline_points SET edited = ?3 WHERE device_id = ?1 AND kind = 'input' AND idx = ?2";
        else
            sql = "INSERT INTO offline_points (device_id, kind, idx, base, edited)"
                  " VALUES (?1, 'input', ?2, ?4, ?3)";

        Statement write(m_db, sql);
        write.bind(1, id);
        write.bind(2, (int64_t)point.index);
        if (point.edited != base)
        {
            write.bind_blob(3, point.edited.data(), point.edited.size());
            if (had != Statement::Step::Row)
                write.bind_blob(4, base.data(), base.size());
        }
        if (write.step() != Statement::Step::Done)
        {
            error = write.error();
            return false;
        }

        if (!t.commit())
        {
            error = t.error();
            return false;
        }
        return true;
    }

    bool DeviceDb::revert_offline_input(uint32_t serial, int index, std::string& error)
    {
        // One statement, so all or nothing without a transaction.
        Statement q(m_db,
                    "DELETE FROM offline_points WHERE kind = 'input' AND idx = ?2 AND device_id ="
                    " (SELECT id FROM devices WHERE kind = 'scanned' AND serial = ?1)");
        q.bind(1, (int64_t)serial);
        q.bind(2, (int64_t)index);
        if (q.step() != Statement::Step::Done)
        {
            error = q.error();
            return false;
        }
        return true;
    }

    bool DeviceDb::replace_offline_inputs(uint32_t serial, const std::vector<OfflinePoint>& points,
                                          std::string& error)
    {
        // Every point first, so a bad one refuses the lot before anything
        // is deleted.
        for (const auto& point : points)
        {
            if (point.base.size() != wire::kInputPointWireSize || point.edited.size() != wire::kInputPointWireSize)
            {
                error = "an input is " + std::to_string(wire::kInputPointWireSize) + " bytes";
                return false;
            }
            if (point.index < 0 || point.index > 254)
            {
                error = "input " + std::to_string(point.index + 1) + " is not one a panel can have";
                return false;
            }
        }

        Transaction t(m_db);
        if (!t.began())
        {
            error = t.error();
            return false;
        }

        Statement device(m_db, "SELECT id FROM devices WHERE kind = 'scanned' AND serial = ?1");
        device.bind(1, (int64_t)serial);
        const Statement::Step found = device.step();
        if (found == Statement::Step::Error)
        {
            error = device.error();
            return false;
        }
        if (found != Statement::Step::Row)
        {
            error = "serial " + std::to_string(serial) + " is not in the saved list";
            return false;
        }
        const int64_t id = device.column_int(0);

        Statement clear(m_db, "DELETE FROM offline_points WHERE device_id = ?1 AND kind = 'input'");
        clear.bind(1, id);
        if (clear.step() != Statement::Step::Done)
        {
            error = clear.error();
            return false;
        }

        for (const auto& point : points)
        {
            if (point.edited == point.base)
                continue;

            // The table's key refuses a second change to one input.
            Statement write(m_db, "INSERT INTO offline_points (device_id, kind, idx, base, edited)"
                                  " VALUES (?1, 'input', ?2, ?3, ?4)");
            write.bind(1, id);
            write.bind(2, (int64_t)point.index);
            write.bind_blob(3, point.base.data(), point.base.size());
            write.bind_blob(4, point.edited.data(), point.edited.size());
            if (write.step() != Statement::Step::Done)
            {
                error = write.error();
                return false;
            }
        }

        if (!t.commit())
        {
            error = t.error();
            return false;
        }
        return true;
    }

    bool DeviceDb::references_held()
    {
        Statement q(m_db, "PRAGMA foreign_keys");
        return q.step() == Statement::Step::Row && q.column_int(0) == 1;
    }
}
