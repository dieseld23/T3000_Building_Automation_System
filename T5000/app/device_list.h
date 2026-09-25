#pragma once

// The device list as the routes change it: the registry in memory and the
// saved copy on disk, kept in step.
//
// Here rather than in main.cpp so it can be tested against a database in
// memory, without a server or a subnet. Each function changes the registry
// only after the file has taken the change, so the two cannot disagree about
// what is saved: a failed save leaves both as they were, and says why.
//
// If the database could not be opened, T5000 still runs. The list lives in
// memory for the session, and every response carries StoreStatus, so the page
// says the list is not being saved rather than letting the operator find out
// at the next start.

#include <stdint.h>
#include <string>

#include "../device/registry.h"
#include "../discovery/scanner.h"
#include "../store/device_db.h"
#include "scan_json.h"

namespace t5000::app
{
    // Opens the database at `path` and loads what it holds into `registry`,
    // which is expected to be empty. On failure the registry is untouched and
    // the status says why.
    StoreStatus open_saved_list(store::DeviceDb& db, const std::string& path,
                                device::Registry& registry);

    // Takes in one scan's results.
    //
    // Numbers the scan, marks each device it found as having answered it at
    // `now`, merges them into the registry, re-derives the duplicate Modbus
    // ids, and saves every device that has answered a scan this session.
    // `summary` gets the scan's statistics; `status.error` gets any failure
    // to save, and is cleared by a save that works, which then includes
    // whatever an earlier failure left unsaved.
    void record_scan(device::Registry& registry, store::DeviceDb& db,
                     const discovery::ScanResult& result, int64_t now,
                     ScanSummary& summary, StoreStatus& status);

    // What the operator can do to the list. Each returns false, and leaves
    // everything as it was, with `message` saying why.
    //
    // Forgetting removes a device from the list and the file, with its name
    // and location. It does nothing to the device, which will be listed
    // again if it answers a later scan.
    //
    // The duplicate Modbus ids are worked out again afterwards, and the
    // summary's count with them. The device forgotten may have been one of a
    // pair, and the other must stop being accused of sharing an id with a
    // device that is no longer on the page.
    bool forget_device(device::Registry& registry, store::DeviceDb& db, device::Handle handle,
                       ScanSummary& summary, std::string& message);
    bool forget_all(device::Registry& registry, store::DeviceDb& db, std::string& message);

    // Gives a device a name and a location, saved in the file. Refused for a
    // device with no serial, which cannot be saved, and when the file is not
    // open, since the name would be lost when T5000 closes.
    bool place_device(device::Registry& registry, store::DeviceDb& db, device::Handle handle,
                      const device::Placement& placement, const StoreStatus& status,
                      std::string& message);

    // The longest name or location kept, in characters.
    constexpr int kMaxPlacementChars = 60;

    // Trims each field, and refuses one that is too long, holds a control
    // character, or is not valid UTF-8.
    bool clean_placement(device::Placement& placement, std::string& message);

    // The bodies the page sends: {"handle":"12"}, and the same with name,
    // building, floor and room. Read with json::parse_flat_object, because
    // these carry text someone typed. A field left out is left empty.
    bool read_handle_request(const std::string& body, device::Handle& handle, std::string& message);
    bool read_placement_request(const std::string& body, device::Handle& handle,
                                device::Placement& placement, std::string& message);
}
