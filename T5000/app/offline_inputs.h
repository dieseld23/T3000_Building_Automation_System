#pragma once

// The inputs of a device added by hand, configured before it is found, by a
// scan or by Find: whether they can be, the configuration as the Inputs page
// shows it, and the operator's changes to it.
//
// Nothing here sends anything to a device, and nothing here can: it reads and
// writes T5000.db, through store::DeviceDb, and includes no transport. The
// configuration waits there until T5000 has a write path, which will compare
// each change with what the device holds before writing it.
//
// The rules for a change are T3000's (offline/input_edit.h). What a device
// added by hand is, and why nothing is read from it, is plan_points_read's
// (app/inputs_plan.h).

#include <string>
#include <vector>

#include "../device/product.h"
#include "../device/registry.h"
#include "../offline/input_edit.h"
#include "../store/device_db.h"
#include "scan_json.h"

namespace t5000::app
{
    // Whether a device's inputs can be configured offline, and how many.
    struct OfflineInputsPlan
    {
        bool        can_edit = false;
        std::string reason;   // when !can_edit, a sentence for the page

        device::ProductClassId product = device::ProductClassId::Unknown;
        device::MiniType       type    = device::MiniType::NotSet;
        std::string            model;   // the model's name, "T3-OEM"

        // How many inputs T3000 holds for the model, and how many of them its
        // grid shows (INPUT_LIMITE_ITEM_COUNT). Nothing is read, so there are
        // no settings to size them from: 64, as for a panel T3000 has not read.
        int inputs = 0;
        int rows   = 0;

        // The panel as the rules for a change need it.
        offline::InputPanel panel() const;
    };

    // An entry added by hand that has not been found, by a scan or by Find.
    // The one kind of device whose Inputs page shows a configuration kept by
    // T5000 rather than one read from the device.
    bool is_configured_offline(const device::DeviceRecord& d);

    // Refused, with the reason, for a device a scan has found; for a product
    // T5000 does not read by BACnet private transfer, since those are the
    // ones whose inputs are Str_in_point; and until the model is chosen. The
    // model decides how T3000 shows some inputs, and which ranges can be
    // changed, so the inputs are not configured without it.
    OfflineInputsPlan plan_offline_inputs(const device::DeviceRecord& d);

    // A device's inputs as configured: each as T3000 starts it, with the
    // changes saved for it over it.
    struct OfflineInputs
    {
        std::vector<offline::InputBytes> inputs;   // plan.inputs of them
        std::vector<offline::InputBytes> bases;    // what each started from

        // Saved changes to inputs past plan.inputs. Kept in the file, not
        // shown, and not changed. None unless the model was changed to one
        // with fewer inputs, which place_device refuses.
        int beyond = 0;
    };

    bool load_offline_inputs(store::DeviceDb& db, const device::DeviceRecord& d, const OfflineInputsPlan& plan,
                             OfflineInputs& out, std::string& error);

    // What the Inputs page is sent for a device configured offline: the
    // configuration, or why there is none.
    std::string offline_inputs_payload(store::DeviceDb& db, const StoreStatus& status, const device::DeviceRecord& d);

    // A change as the page sends it:
    // {"handle":"12","index":"3","field":"label","value":"AHU"}. index is
    // 0-based. value is text as typed, "Auto"/"Manual", or for a range its
    // number in T3000's Range dialog, "41".
    struct InputEditRequest
    {
        device::Handle      handle = device::kNoHandle;
        int                 index  = -1;
        offline::InputField field  = offline::InputField::FullLabel;
        std::string         value;
    };

    bool read_input_edit_request(const std::string& body, InputEditRequest& request, std::string& message);

    // {"handle":"12","index":"3"}: undo every change to one input.
    bool read_input_revert_request(const std::string& body, device::Handle& handle, int& index,
                                   std::string& message);

    // Makes the change and saves it, or refuses, with `message` saying why
    // and the file as it was. Refused when the list is not being saved, since
    // the change would be lost when T5000 closes.
    bool edit_offline_input(const device::Registry& registry, store::DeviceDb& db, const StoreStatus& status,
                            const InputEditRequest& request, std::string& message);

    // Puts one input back to what it started as.
    bool revert_offline_input(const device::Registry& registry, store::DeviceDb& db, const StoreStatus& status,
                              device::Handle handle, int index, std::string& message);

    // An import as the page sends it: {"handle":"12","file":"VQ8I..."}, the
    // .prog file in base64. With "check":true, nothing is saved, and the
    // answer says what the import would do, for the page to ask first.
    struct InputImportRequest
    {
        device::Handle       handle = device::kNoHandle;
        std::vector<uint8_t> file;
        bool                 check = false;
    };

    bool read_input_import_request(const std::string& body, InputImportRequest& request, std::string& message);

    // Takes a device's inputs from a .prog file T3000 saved from it, in place
    // of every change made here to its inputs. Of each input shown for the
    // model, what the operator sets is kept (offline/prog_file.h,
    // imported_input); nothing else in the file is.
    //
    // Refused, with `message` saying why and the list as it was, as a change
    // is (a device a scan has found, no model, a list not being saved), and
    // for a file that is not a .prog file T5000 reads, one whose settings
    // give another serial, or 0, and one saved from another model. With
    // request.check, `message` says what the import would do, and nothing
    // is saved; without, what it did.
    bool import_offline_inputs(const device::Registry& registry, store::DeviceDb& db, const StoreStatus& status,
                               const InputImportRequest& request, std::string& message);

    // For a device read from the network: that inputs were changed offline
    // before it was found, and wait unwritten. Empty when none were.
    std::string pending_offline_note(store::DeviceDb& db, const device::DeviceRecord& d);

    // How many inputs are saved with changes for a device, for place_device's
    // check. False, with `error`, when the file could not be read.
    bool offline_input_indexes(store::DeviceDb& db, uint32_t serial, std::vector<int>& indexes, std::string& error);
}
