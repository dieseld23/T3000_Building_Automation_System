#pragma once

// The saved device list: every device that has answered a scan, and every one
// the operator has added by hand, kept between runs, as T3000 keeps its
// building database.
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
// A device added by hand is saved with only what the operator gave: its
// serial, its product and its placement. A scan that later finds that serial
// updates the same row, as it would any saved device, so the entry and the
// device it stood for cannot become two.
//
// Only devices with a usable serial are saved. The serial is the key, and a
// device reporting 0 or 0xFFFFFFFF cannot be told apart from another one in
// the same state, so there is nothing to key it on. The table refuses such a
// row as well, so this does not rest on every caller remembering.
//
// It also holds the configuration the operator has prepared for a device
// that cannot be reached yet: each point changed, kept as the ud_str.h bytes
// it goes to the device as, with the bytes the change started from. Only the
// points changed are kept; a point with no row is as it started.
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
    //
    // 1: the devices table.
    // 2: added_by_hand, for a device the operator added before it was found.
    // 3: offline_points, for a device's points changed before it can be
    //    reached.
    constexpr int kSchemaVersion = 4;

    // One point the operator has changed on a device that cannot be reached:
    // the bytes it goes to the device as (Str_in_point for an input), before
    // and after.
    //
    // The two are kept, not only the result, so the change can be told from
    // what it started from. When the device is reachable and T5000 can write,
    // only the fields that differ are candidates, and each is compared with
    // what the device holds by then: a field the device has changed since is
    // shown to the operator rather than overwritten.
    // Which saved row a call is about. A real device's row is kind
    // 'scanned', whether a scan found it or it was added by hand; a virtual
    // device's is kind 'virtual'. The serial alone is not enough: a real
    // device can answer with a serial T5000 gave a virtual device
    // (device::kFirstVirtualSerial), and the two are two rows.
    enum class DeviceKind
    {
        Scanned,
        Virtual,
    };

    struct DeviceKey
    {
        DeviceKind kind   = DeviceKind::Scanned;
        uint32_t   serial = 0;
    };

    inline DeviceKey scanned_key(uint32_t serial) { return DeviceKey{ DeviceKind::Scanned, serial }; }
    inline DeviceKey virtual_key(uint32_t serial) { return DeviceKey{ DeviceKind::Virtual, serial }; }

    // The row a device in the registry is saved as.
    inline DeviceKey key_of(const device::DeviceRecord& d)
    {
        return d.is_virtual() ? virtual_key(d.serial_number) : scanned_key(d.serial_number);
    }

    // The kind column's value: "scanned" or "virtual".
    const char* kind_name(DeviceKind kind);

    struct OfflinePoint
    {
        int                  index = 0;   // 0-based, as T3000 numbers points
        std::vector<uint8_t> base;        // what the first change started from
        std::vector<uint8_t> edited;      // what the operator has made it
    };

    // T5000.db, beside the executable, as UTF-8. Beside it rather than in
    // %APPDATA% for the same reason as the connection settings: this is a tool
    // a technician carries on a USB stick, and the list goes with it.
    std::string default_db_path();

    class DeviceDb
    {
    public:
        // Opens the file, creating it and its table if it is new, and bringing
        // a list an older T5000 wrote up to this build's schema.
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

        // Every saved device, of either kind, in the order each was first
        // saved, with answered_scan 0. A virtual device comes back as
        // Provenance::Virtual and a device added by hand that has not been
        // found yet as Provenance::ManuallyAdded, neither of them reached;
        // every other one - found by a scan or by Find - as
        // Provenance::Restored.
        bool load(std::vector<device::DeviceRecord>& out, std::string& error);

        // Saves what a scan, or Find, found about these devices, in one
        // transaction.
        //
        // A device already saved has its observed fields and last_seen
        // updated, and keeps its name, location and first_seen. A new one is
        // added whole. A device with no usable serial is skipped.
        bool save_scanned(const std::vector<device::DeviceRecord>& devices, std::string& error);

        // Saves the operator's name and location for one device. Adds the
        // device if it is not saved yet.
        bool save_placement(const device::DeviceRecord& device, std::string& error);

        // Saves a device the operator has added by hand, with its placement.
        //
        // Only ever adds. Refused when a device with that serial is already
        // saved, however it got there, so an entry typed in cannot overwrite
        // what a scan learned about a real device. Refused too for a serial
        // that is not a usable key.
        bool add_by_hand(const device::DeviceRecord& device, std::string& error);

        // Saves a virtual device the operator has made (app/device_list.h),
        // as kind 'virtual'. Only ever adds: refused when a virtual device
        // with that serial is saved, and for a record that is not a virtual
        // device's or whose serial is not one T5000 hands out.
        bool add_virtual(const device::DeviceRecord& device, std::string& error);

        // Deletes one saved device, name, location and offline changes
        // included, in one transaction. Not an error when it was not saved.
        bool forget(const DeviceKey& key, std::string& error);

        // Deletes every device: those found by a scan, those added by hand
        // and the virtual ones, with every offline change, in one
        // transaction.
        bool forget_all(std::string& error);

        // The inputs changed offline on the saved device with this serial, in
        // index order. None for a device with no changes, or not saved.
        bool load_offline_inputs(const DeviceKey& key, std::vector<OfflinePoint>& out, std::string& error);

        // Saves one input's change, in one transaction.
        //
        // An input already changed keeps the base it has: that is what the
        // first change started from, whatever this one did. The row goes
        // when the input is made what its base is again, so a change undone
        // by hand leaves nothing to write. Refused for a device that is not
        // saved, and for bytes that are not one input.
        bool save_offline_input(const DeviceKey& key, const OfflinePoint& point, std::string& error);

        // Undoes every change to one input: it goes back to its base. Not an
        // error when it had none.
        bool revert_offline_input(const DeviceKey& key, int index, std::string& error);

        // Puts these changes in place of every input change saved for the
        // device, in one transaction: an import of a .prog file's inputs.
        // A point whose edited bytes are its base is not saved, as for one
        // change. Refused, with nothing changed, for a device that is not
        // saved and for any point that save_offline_input would refuse.
        bool replace_offline_inputs(const DeviceKey& key, const std::vector<OfflinePoint>& points,
                                    std::string& error);

        // Whether this connection holds offline_points to their device, as
        // open() sets it to. Without it a change could outlive its device
        // and be handed to the next device given that row. For the
        // self-test, which cannot see it any other way.
        bool references_held();

    private:
        bool write(const device::DeviceRecord& d, bool with_placement, std::string& error);
        bool insert(const device::DeviceRecord& d, bool added_by_hand, std::string& error);

        Database    m_db;
        std::string m_path;
    };
}
