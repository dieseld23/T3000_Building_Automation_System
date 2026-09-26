#pragma once

// The reads a points page starts with, whichever points it shows:
//
//   1. the panel's settings, checked against the serial expected at the
//      address                              READ_SETTING_COMMAND        1 request
//   2. its custom digital range names       READUNIT_T3000              1 request
//   3. its custom analog table names        READANALOG_CUS_TABLE_T3000  2 requests
//
// T3000 sends all three when it connects to a panel (BacnetView.cpp:5905,
// :6472, :6563-6565). A page asks for the ones its grid uses: Inputs all
// three, Outputs the first two. inputs_read.h says how each failure is
// handled, and why.

#include <stdint.h>

#include <string>

#include "../bacnet/point_read.h"
#include "../device/product.h"
#include "../display/custom_ranges.h"
#include "../wire/panel.h"

namespace t5000::app
{
    // How sure T5000 already is that the panel at the address is the device
    // expected there, before its settings are read.
    //
    // Either way, settings that give another serial stop the read. The two
    // differ over settings that cannot confirm the serial: refused, not
    // decodable, or carrying serial 0.
    enum class Identity
    {
        // It answered a scan this session, from this address, with this
        // serial. The scan has vouched for it, so a panel whose settings
        // cannot confirm the serial is read on, and the page says so.
        VouchedForByScan,

        // Known only from the saved list: it has not answered a scan since
        // T5000 started, and the address is the one it had then. Another
        // panel may have that address now, so unless the settings confirm
        // the serial, nothing more is read and the page says to scan first.
        MustConfirm,
    };

    struct PanelRead
    {
        // The settings answered and decoded.
        bool                settings_known = false;
        wire::PanelSettings settings;

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

    // Between reads, as between the requests within one.
    void pause_between_reads(const bacnet::ReadSettings& settings);
}
