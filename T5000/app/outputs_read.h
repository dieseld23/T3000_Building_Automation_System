#pragma once

// Reading what the Outputs page shows, over one transport:
//
//   1. the panel's settings                 READ_SETTING_COMMAND        1 request
//   2. its custom digital range names       READUNIT_T3000              1 request
//   3. its outputs                          READOUTPUT_T3000            7 requests
//
// T3000 sends 1 and 2 when it connects to a panel, with the custom analog
// table names besides (panel_read.h), and 3 when the Outputs list is loaded.
// The analog table names are not asked for here: they name input ranges
// 20-24, and no output range uses them.
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
#include "../wire/points.h"
#include "panel_read.h"

namespace t5000::app
{
    struct OutputsPageRead
    {
        // The outputs were read. When false, `error` says why and nothing is
        // shown.
        bool        ok = false;
        std::string error;

        PanelRead                      panel;
        std::vector<wire::OutputPoint> points;  // every output read; index is the point number

        int requests_sent = 0;
    };

    // expected_serial is the serial the device was found with: by this
    // session's scan, or in the saved list, as `identity` says.
    OutputsPageRead read_outputs_page(bacnet::ReadTransport& transport, const bacnet::Endpoint& device,
                                      device::ProductClassId product, uint32_t expected_serial,
                                      Identity identity, const bacnet::ReadSettings& settings,
                                      uint8_t& next_invoke_id);
}
