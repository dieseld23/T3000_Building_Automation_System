#pragma once

// Reading what the Variables page shows, over one transport:
//
//   1. the panel's settings                 READ_SETTING_COMMAND    1 request
//   2. its custom digital range names       READUNIT_T3000          1 request
//   3. its multi-state tables               READ_MSV_COMMAND        2 requests
//   4. its custom variable units            READVARUNIT_T3000       1 request
//   5. its variables                        READVARIABLE_T3000     13 requests
//
// T3000 sends 1-4 when it connects to a panel (BacnetView.cpp:5906, :6486,
// :6497-6561, :6585), with the custom analog table names between 3 and 4,
// and 5 when the Variables list is loaded (:5384-5403). The analog table
// names are not asked for here: they name input ranges 20-24, and no
// variable range uses them.
//
// T3000 sends 2-4 only to the CM5, TSTAT10, MINIPANEL, MINIPANEL_ARM and
// ESP32 T3 (:6458-6470). Those are the products T5000 reads by private
// transfer at all (device/read_path.cpp), so every panel read here is one
// T3000 would ask.
//
// Each failure is handled as the Inputs page handles it (inputs_read.h):
// silence to the settings stops the page, a refusal is read past unless
// the device is known only from the saved list, a serial that is not the
// one expected stops the page, and missing names are a note.

#include <stdint.h>

#include <string>
#include <vector>

#include "../bacnet/point_read.h"
#include "../device/product.h"
#include "../display/variable_ranges.h"
#include "../wire/points.h"
#include "panel_read.h"

namespace t5000::app
{
    struct VariablesPageRead
    {
        // The variables were read. When false, `error` says why and nothing
        // is shown.
        bool        ok = false;
        std::string error;

        PanelRead                        panel;
        display::VariableRanges          names;   // custom units and multi-state tables
        std::vector<wire::VariablePoint> points;  // every variable read; index is the point number

        int requests_sent = 0;
    };

    // expected_serial is the serial the device was found with: by this
    // session's scan, by Find, or in the saved list, as `identity` says.
    VariablesPageRead read_variables_page(bacnet::ReadTransport& transport, const bacnet::Endpoint& device,
                                          device::ProductClassId product, uint32_t expected_serial,
                                          Identity identity, const bacnet::ReadSettings& settings,
                                          uint8_t& next_invoke_id);
}
