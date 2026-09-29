#pragma once

// The reads a points page starts with, whichever points it shows:
//
//   1. the panel's settings, checked against the serial expected at the
//      address                              READ_SETTING_COMMAND        1 request
//   2. its custom digital range names       READUNIT_T3000              1 request
//   3. its custom analog table names        READANALOG_CUS_TABLE_T3000  2 requests
//   4. its multi-state tables               READ_MSV_COMMAND            2 requests
//   5. its custom variable units            READVARUNIT_T3000           1 request
//
// T3000 sends all five when it connects to a panel (BacnetView.cpp:5905,
// :6472, :6483-6547, :6563-6571). A page asks for the ones its grid uses:
// Inputs 1-3, Outputs 1-2, Variables 1, 2, 4 and 5. inputs_read.h says how
// each failure is handled, and why.

#include <stdint.h>

#include <string>

#include "../bacnet/point_read.h"
#include "../device/product.h"
#include "../device/registry.h"
#include "../display/custom_ranges.h"
#include "../display/variable_ranges.h"
#include "../wire/panel.h"

namespace t5000::app
{
    // How sure T5000 already is that the panel at the address is the device
    // expected there, before its settings are read.
    //
    // Whichever it is, settings that give another serial stop the read. Only
    // VouchedForByScan reads on from settings that cannot confirm the serial:
    // refused, not decodable, or carrying serial 0. The other two stop, and
    // differ only in what the page is told to do next.
    enum class Identity
    {
        // It answered a scan this session, from this address, with this
        // serial. The scan has vouched for it, so a panel whose settings
        // cannot confirm the serial is read on, and the page says so.
        VouchedForByScan,

        // Known only from the saved list: it has not answered a scan since
        // T5000 started, and the address is the one it had then. Another
        // panel may have that address now, so unless the settings confirm
        // the serial, nothing more is read and the page says to scan, or to
        // find it at its address, first.
        MustConfirm,

        // Found by Find this session (app/find_device.h), at an address the
        // operator gave, and not by a scan. Its settings gave its serial
        // there once; the address is still only the operator's word, so it
        // is held to MustConfirm's rule, and the page says to find it again.
        FoundAtAddress,
    };

    struct PanelRead;

    // Keeps the bootloader's version from the settings a read left, for
    // the device with this handle, through Registry::note_bootloader,
    // which keeps it only when their serial is the device's own. Nothing
    // when the settings did not decode, or give 0: a panel that does not
    // report its bootloader leaves the byte 0, and ISP passes over 0 as
    // no version where it reads one (a TSTAT8's, global_function.cpp:1083).
    // `by` names the read, for the Firmware page and its reasons: "Find",
    // "the Inputs page".
    bool keep_bootloader(device::Registry& registry, device::Handle handle, const PanelRead& panel,
                         const std::string& by);

    struct PanelRead
    {
        // The settings answered and decoded.
        bool                settings_known = false;
        wire::PanelSettings settings;

        // Nothing answered the settings read, sent once more: as T3000
        // takes it, the panel is not connected.
        bool no_answer = false;

        display::CustomRanges ranges;

        // What the page should be told about the panel: why the settings or
        // some names are missing, or that the serial could not be checked.
        // Empty when everything T3000 would have read was read.
        std::string note;
    };

    // How a page names itself and its points in what it tells the operator.
    struct PageWords
    {
        const char* page;     // "Inputs", as in "open Inputs again"
        const char* points;   // "inputs"

        // What the page shows when the settings could not be read, as the
        // end of "T5000 reads the inputs anyway, but without the settings: ".
        const char* without_settings;

        // How many points are read when the settings do not say: 64 inputs
        // or outputs, 128 variables.
        int usual_count;
    };

    // 1. The settings. Returns false, with `error` saying why, when nothing
    // more should be read from this panel.
    bool read_panel_settings(bacnet::ReadTransport& transport, const bacnet::Endpoint& device,
                             device::ProductClassId product, uint32_t expected_serial, Identity identity,
                             const bacnet::ReadSettings& settings, uint8_t& next_invoke_id,
                             const PageWords& words, PanelRead& panel, std::string& error,
                             int& requests_sent);

    // 2. The custom digital range names. A refusal or silence leaves them
    // missing, with a note, rather than stopping the page.
    void read_digital_names(bacnet::ReadTransport& transport, const bacnet::Endpoint& device,
                            const bacnet::ReadSettings& settings, uint8_t& next_invoke_id,
                            PanelRead& panel, int& requests_sent);

    // 3. The custom analog table names: 0-3, and then 4 only if 0-3 came
    // back, as T3000 does. The same, for a failure.
    void read_analog_tables(bacnet::ReadTransport& transport, const bacnet::Endpoint& device,
                            const bacnet::ReadSettings& settings, uint8_t& next_invoke_id,
                            PanelRead& panel, int& requests_sent);

    // 4. The multi-state tables: 0-1, and then 2 on firmware 60.7 and
    // older or 2-3 on newer, each request sent whether or not the one before
    // came back, as T3000 sends them (BacnetView.cpp:6483-6547). Without the
    // settings, as on older firmware. A request that fails is a note.
    void read_msv_tables(bacnet::ReadTransport& transport, const bacnet::Endpoint& device,
                         const bacnet::ReadSettings& settings, uint8_t& next_invoke_id,
                         PanelRead& panel, display::VariableRanges& names, int& requests_sent);

    // The last firmware T3000 reads three multi-state tables from rather
    // than four: firmware0_rev_main * 10 + firmware0_rev_sub <= 607
    // (BacnetView.cpp:6483).
    inline constexpr int kLastThreeTableFirmware = 607;

    // 5. The custom variable units, 0-4 in one request (:6571). The same,
    // for a failure.
    void read_variable_units(bacnet::ReadTransport& transport, const bacnet::Endpoint& device,
                             const bacnet::ReadSettings& settings, uint8_t& next_invoke_id,
                             PanelRead& panel, display::VariableRanges& names, int& requests_sent);

    // Between reads, as between the requests within one.
    void pause_between_reads(const bacnet::ReadSettings& settings);
}
