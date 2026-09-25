#pragma once

// The device list, the scan that produced it, and the interfaces it can run
// on, as JSON.
//
// Kept apart from the scanner for the same reason points_json is kept apart
// from the decoder: deciding what a scan MEANS is worth testing without a
// subnet, and a second transport later should not have to reinvent the
// encoding.

#include <string>
#include <vector>

#include "../device/registry.h"
#include "../discovery/scanner.h"
#include "../net/interfaces.h"

namespace t5000::app
{
    // Why the device list looks the way it does.
    //
    // "Empty" is not one state, and a page that renders one message for all of
    // them tells the operator nothing. Never scanned, scanned and the subnet
    // is genuinely empty, scanned but devices answered unreadably, and the
    // scan could not run at all are four different problems with four
    // different next steps.
    struct ScanSummary
    {
        bool has_scanned = false;

        discovery::ScanStats stats;

        // Set only when the scan could not run. An empty list with no error
        // is a real answer, not a failure.
        std::string error;

        // Which interface the last scan went out of, as text, so the "nothing
        // found" message can name it. Empty means all interfaces.
        std::string interface_ip;

        // How long the last scan waited, so the page can offer to wait longer
        // rather than just repeating the same thing.
        int waited_ms = 0;
    };

    // Whether the device list is being saved, and if not, why.
    //
    // Shown on the page rather than only printed at startup. A list that has
    // quietly stopped being saved loses every name and location typed into
    // it the next time T5000 closes.
    struct StoreStatus
    {
        bool        saving = false;   // the database is open
        std::string path;             // the file, or the one that could not be opened
        std::string error;            // why it is not open, or why the last save failed
        int         restored = 0;     // devices loaded from it at startup
    };

    std::string build_devices_json(const device::Registry& registry,
                                   const ScanSummary& summary,
                                   const StoreStatus& store = StoreStatus());

    // One device in full, for a detail pane.
    //
    // NOT YET SERVED BY ANY ROUTE. Groundwork, with tests, so that its passing
    // tests are not mistaken for a working endpoint: /api/device currently
    // answers with build_product_json instead.
    //
    // Returns a JSON null when the handle does not resolve - a page can be holding a handle for a device
    // that has since gone, and that is worth saying rather than guessing.
    std::string build_device_json(const device::Registry& registry,
                                  device::Handle handle);

    std::string build_interfaces_json(const std::vector<net::Interface>& interfaces,
                                      const std::string& error);
}
