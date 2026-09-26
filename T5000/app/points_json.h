#pragma once

// Turning decoded points into the JSON the page consumes.
//
// Kept separate from both the decoder and the HTTP server so it can be tested
// on its own, and so a second transport later (a websocket push, say) does not
// have to reimplement the encoding.

#include <string>
#include <vector>

#include "../device/product.h"
#include "../device/read_path.h"
#include "../display/custom_ranges.h"
#include "../display/variable_ranges.h"
#include "../wire/panel.h"
#include "../wire/points.h"

namespace t5000::app
{
    // Device text arrives as CP_ACP bytes, which on these controllers means GBK.
    // JSON is UTF-8 by definition, so every string crossing this boundary has to
    // be converted rather than copied - skipping this is precisely what turns
    // non-ASCII point labels into mojibake on the page.
    std::string acp_to_utf8(const uint8_t* text, size_t max_length);

    // Escapes for a JSON string literal. Control characters are escaped as \u
    // rather than dropped, because a stray byte in device text should be visible
    // as an oddity, not silently vanish.
    std::string json_escape(const std::string& utf8);

    struct DeviceInfo
    {
        int serial_number   = 0;
        int product_id      = 0;
        int firmware        = 0;
        int protocol        = 0;
        bool is_fixture     = false;   // not real device data

        // True only when these points just came off the wire from the device.
        //
        // The page says "read from serial NNN" on THIS, and on nothing else.
        // It used to say it whenever isFixture was false, which made the
        // claim the default: any payload that forgot a flag asserted a
        // reading. A positive statement cannot be produced by omission.
        bool read_from_wire = false;
        std::string address;           // where it was read from, when it was

        // When the device has not answered a scan since T5000 started: when
        // it was last seen, and that its address comes from the saved list.
        // Empty for a device this session's scan found.
        std::string sighting;
    };

    // The panel the inputs came from, as far as it was read.
    struct InputsPanel
    {
        bool                   known = false;   // its settings were read
        wire::PanelSettings    settings;
        device::ProductClassId product = device::ProductClassId::Unknown;
        display::CustomRanges  ranges;

        // What the read found worth saying. The row limit adds to it.
        std::string note;
    };

    // The whole payload: which device, how it was read (and why), and the points.
    //
    // The read-path decision travels WITH the data deliberately. A grid that is
    // empty because the firmware predates the PTP tunnel must be able to say so
    // on the page; that is the failure this project exists to stop repeating.
    //
    // Only the rows T3000 shows with anything in them are included: a model
    // with 8 inputs has rows 9-64 blank in T3000, and here they are left out,
    // with the panel note saying so. Each row keeps its own index and input
    // number.
    std::string build_inputs_json(const DeviceInfo& device,
                                  const device::Decision& decision,
                                  const std::vector<wire::InputPoint>& points,
                                  const InputsPanel& panel = InputsPanel());

    // What the Inputs page shows for a device configured offline, beside its
    // inputs.
    struct OfflineInputsView
    {
        // The device's handle, which a change to one of its inputs names.
        uint64_t handle = 0;

        std::string model;      // the model chosen for it, "T3-OEM"
        device::MiniType type = device::MiniType::NotSet;

        // Whether the list is being saved. When it is not, a change could
        // not be kept, and the page says so rather than offering one.
        bool saving = false;

        int edited = 0;         // inputs with changes
        size_t rows = 0;        // how many T3000 shows for the model

        // The columns that can be changed, by their payload names.
        std::vector<std::string> editable;

        // For each input, the columns changed from what it started as.
        std::vector<std::vector<std::string>> changed;

        std::string note;        // for the page's banner
        std::string detail;      // readPath.detail: that nothing is read or sent
        std::string panel_note;  // panel.note
    };

    // The Inputs page's payload for a device configured offline: shaped as
    // build_inputs_json's, so the page shows it with the same grid, with an
    // "offline" object first and each row's "changed" columns.
    //
    // readFromWire is false and readPath says nothing was read: these inputs
    // are T5000's, not a device's. They are shown as T3000 would show them
    // after reading them back from the device.
    std::string build_offline_inputs_json(const DeviceInfo& device, const std::vector<wire::InputPoint>& points,
                                          const OfflineInputsView& view);

    // The payload for a device the tool has FOUND but cannot read.
    //
    // It must carry every field build_inputs_json carries, because the page
    // reads them unconditionally and its defaults are not safe. A missing
    // isFixture reads as false, and the page renders false as "Live device.
    // Points below were read from serial NNN" - over an empty grid, asserting
    // a reading that never happened. That is the exact failure this project
    // exists to stop, and it shipped in the commit that removed the other one.
    //
    // Hence a builder rather than a string literal at the call site: the shape
    // is a contract with the page, and contracts belong somewhere testable.
    //
    // `sighting` is DeviceInfo::sighting: said beside the reason, for a
    // device known only from the saved list.
    std::string build_unavailable_inputs_json(int serial_number,
                                              const std::string& address,
                                              const std::string& reason,
                                              const std::string& sighting = std::string());

    // The panel the outputs came from: the same as for inputs.
    using OutputsPanel = InputsPanel;

    // The Outputs page's payload, shaped as the Inputs page's is: the same
    // device, readPath and panel keys, "outputs" where that has "inputs",
    // and only the custom digital ranges - no output range uses the analog
    // tables, so they are not read.
    //
    // Only the rows T3000 shows with anything in them are included, as for
    // inputs; a model with no outputs has none.
    std::string build_outputs_json(const DeviceInfo& device,
                                   const device::Decision& decision,
                                   const std::vector<wire::OutputPoint>& points,
                                   const OutputsPanel& panel = OutputsPanel());

    // The payload for a device the tool has found but cannot read the
    // outputs of. Every key build_outputs_json writes is here too, for the
    // reason build_unavailable_inputs_json gives.
    std::string build_unavailable_outputs_json(int serial_number,
                                               const std::string& address,
                                               const std::string& reason,
                                               const std::string& sighting = std::string());

    // The panel the variables came from: as for inputs, with the names only
    // variables use.
    struct VariablesPanel : InputsPanel
    {
        display::VariableRanges names;
    };

    // The Variables page's payload, shaped as the others are: the same
    // device, readPath and panel keys, "variables" where they have "inputs",
    // and in customRanges the digital range names, the custom units and the
    // multi-state tables' names - no variable range uses the analog tables.
    //
    // Every variable read is included: T3000 has no per-model limit on the
    // Variables grid.
    std::string build_variables_json(const DeviceInfo& device,
                                     const device::Decision& decision,
                                     const std::vector<wire::VariablePoint>& points,
                                     const VariablesPanel& panel = VariablesPanel());

    // The payload for a device the tool has found but cannot read the
    // variables of. Every key build_variables_json writes is here too, for
    // the reason build_unavailable_inputs_json gives.
    std::string build_unavailable_variables_json(int serial_number,
                                                 const std::string& address,
                                                 const std::string& reason,
                                                 const std::string& sighting = std::string());
}
