#pragma once

// Reading what the Inputs page shows, in T3000's order, over one transport:
//
//   1. the panel's settings                 READ_SETTING_COMMAND        1 request
//   2. its custom digital range names       READUNIT_T3000              1 request
//   3. its custom analog table names        READANALOG_CUS_TABLE_T3000  2 requests
//   4. its inputs                           READINPUT_T3000             7 requests
//
// T3000 sends 1-3 when it connects to a panel (BacnetView.cpp:5905, :6472,
// :6563-6565) and 4 when the Inputs list is loaded. T5000 has no connection
// to keep them in, so it sends all four each time the page is opened.
//
// Where T5000 departs from T3000, it is to show more rather than less, and
// it says so:
//
//   - T3000 treats a panel whose settings do not come back as offline and
//     reads nothing more (:5905-5955). T5000 does the same when nothing
//     answers - but when the panel answers with a refusal, it goes on to the
//     inputs, without the per-model rules the settings drive, and says so.
//   - A refused or unanswered custom-name read leaves those names missing,
//     with a note, rather than stopping the page. As in T3000, the analog
//     table names are asked for whether or not the digital names came back.
//
// And it is stricter in one place: the settings carry the panel's serial
// number, and a panel whose serial is not the one expected at that address
// is not read further. T3000's own web view makes the same check
// (BacnetWebView.cpp:1655-1665); its Inputs screen does not.
//
// How much further that goes depends on where the address came from; see
// Identity below. For a device known only from the saved list, a serial that
// cannot be read at all is treated like one that does not match.

#include <stdint.h>

#include <string>
#include <vector>

#include "../bacnet/point_read.h"
#include "../device/product.h"
#include "../wire/points.h"
#include "panel_read.h"

namespace t5000::app
{
    // Identity and PanelRead are in panel_read.h, shared with Outputs.

    struct InputsPageRead
    {
        // The inputs were read. When false, `error` says why and nothing is
        // shown - including whatever the earlier reads found, since a page
        // of names with no points is not a partial answer to anything.
        bool        ok = false;
        std::string error;

        PanelRead                     panel;
        std::vector<wire::InputPoint> points;   // every input read; index is the point number

        int requests_sent = 0;
    };

    // expected_serial is the serial the device was found with: by this
    // session's scan, or in the saved list, as `identity` says.
    InputsPageRead read_inputs_page(bacnet::ReadTransport& transport, const bacnet::Endpoint& device,
                                    device::ProductClassId product, uint32_t expected_serial,
                                    Identity identity, const bacnet::ReadSettings& settings,
                                    uint8_t& next_invoke_id);
}
