#include "device_db.h"

#include <windows.h>

namespace t5000::store
{
    namespace
    {
        using namespace t5000::device;

        // Version 1.
        //
        // kind is 'scanned' for a device found on the network. 'virtual' is
        // allowed now for the virtual devices that come next, because SQLite
        // cannot change a CHECK constraint without rebuilding the table.
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

            Transaction t(m_db);
            if (!t.began() ||
                !m_db.exec(kCreateV1, error) ||
                !m_db.exec("PRAGMA user_version = 1", error) ||
                !t.commit())
            {
                if (error.empty())
                    error = t.error();
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
                    "       first_seen, last_seen"
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

            d.placement.name     = q.column_text(12);
            d.placement.building = q.column_text(13);
            d.placement.floor    = q.column_text(14);
            d.placement.room     = q.column_text(15);

            d.first_seen = q.column_int(16);
            d.last_seen  = q.column_int(17);

            // It was saved because it answered a scan, so it has been
            // reached - in an earlier session. answered_scan stays 0: it has
            // not answered one in this session.
            d.provenance           = Provenance::Restored;
            d.reached              = true;
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

        if (found == Statement::Step::Row)
        {
            const int64_t id = find.column_int(0);

            // The observed fields, as the registry holds them after the merge.
            // Not name, building, floor, room or first_seen: those are the
            // operator's, or history, and an observation changes neither.
            Statement u(m_db,
                        "UPDATE devices SET product_class_id = ?2, mini_type = ?3, firmware = ?4,"
                        "       modbus_id = ?5, parent_serial = ?6, object_instance = ?7,"
                        "       host = ?8, answered_from = ?9, reported_ip = ?10,"
                        "       bacnet_port = ?11, panel_name = ?12,"
                        "       last_seen = max(last_seen, ?13)"
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
        const int64_t first = d.first_seen != 0 ? d.first_seen : d.last_seen;

        Statement i(m_db,
                    "INSERT INTO devices (kind, serial, product_class_id, mini_type, firmware,"
                    "       modbus_id, parent_serial, object_instance, host, answered_from,"
                    "       reported_ip, bacnet_port, panel_name, name, building, floor, room,"
                    "       first_seen, last_seen)"
                    " VALUES ('scanned', ?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12,"
                    "         ?13, ?14, ?15, ?16, ?17, ?18)");
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
        if (i.step() != Statement::Step::Done)
        {
            error = i.error();
            return false;
        }
        return true;
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
        Statement q(m_db, "DELETE FROM devices WHERE kind = 'scanned' AND serial = ?1");
        q.bind(1, (int64_t)serial);
        if (q.step() != Statement::Step::Done)
        {
            error = q.error();
            return false;
        }
        return true;
    }

    bool DeviceDb::forget_all_scanned(std::string& error)
    {
        Statement q(m_db, "DELETE FROM devices WHERE kind = 'scanned'");
        if (q.step() != Statement::Step::Done)
        {
            error = q.error();
            return false;
        }
        return true;
    }
}
