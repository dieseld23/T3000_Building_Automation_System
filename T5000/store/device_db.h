#pragma once

// The saved device list: every device that has answered a scan, kept between
// runs, as T3000 keeps its building database.
//
// T5000's own file (T5000.db beside the exe by default), not T3000's. T3000's
// database is one file per building, each with an ALL_NODE table whose
// columns are reused for other things (the IP address is in Bautrate, the
// port in Com_Port). Importing from it is planned; writing into it is not.
//
// What is saved is what a scan learned, plus what the operator has said about
// the device (Placement: a name, a building, a floor, a room). A rescan
// updates the first and never touches the second.
//
// Only devices with a usable serial are saved. The serial is the key, and a
// device reporting 0 or 0xFFFFFFFF cannot be told apart from another one in
// the same state, so there is nothing to key it on. The table refuses such a
// row as well, so this does not rest on every caller remembering.
//
// This is a file on the technician's machine. Nothing here sends anything to
// a device.

#include <stdint.h>
#include <string>
#include <vector>

#include "../device/registry.h"
#include "sqlite.h"

namespace t5000::store
{
    // The schema this build reads and writes, kept in the file's
    // PRAGMA user_version. Raised by every change to the tables, with the
    // step from the previous version added to DeviceDb::open.
    constexpr int kSchemaVersion = 1;

    // T5000.db, beside the executable, as UTF-8. Beside it rather than in
    // %APPDATA% for the same reason as the connection settings: this is a tool
    // a technician carries on a USB stick, and the list goes with it.
    std::string default_db_path();

    class DeviceDb
    {
    public:
        // Opens the file, creating it and its table if it is new.
        //
        // Refuses, and leaves the file as it was:
        //   - a file written by a newer T5000 (a higher user_version), whose
        //     rows this build might misread or damage;
        //   - a SQLite file that is not a T5000 device list, such as one of
        //     T3000's building databases picked by mistake;
        //   - anything that is not a SQLite file.
        bool open(const std::string& path, std::string& error);
        void close();
        bool is_open() const { return m_db.is_open(); }
        const std::string& path() const { return m_path; }

        // Every saved device, in the order each was first saved, as records
        // with Provenance::Restored and answered_scan 0.
        bool load(std::vector<device::DeviceRecord>& out, std::string& error);

        // Saves what a scan found about these devices, in one transaction.
        //
        // A device already saved has its observed fields and last_seen
        // updated, and keeps its name, location and first_seen. A new one is
        // added whole. A device with no usable serial is skipped.
        bool save_scanned(const std::vector<device::DeviceRecord>& devices, std::string& error);

        // Saves the operator's name and location for one device. Adds the
        // device if it is not saved yet.
        bool save_placement(const device::DeviceRecord& device, std::string& error);

        // Deletes one saved device, name and location included. Not an error
        // when it was not saved.
        bool forget(uint32_t serial, std::string& error);

        // Deletes every scanned device.
        bool forget_all_scanned(std::string& error);

    private:
        bool write(const device::DeviceRecord& d, bool with_placement, std::string& error);

        Database    m_db;
        std::string m_path;
    };
}
