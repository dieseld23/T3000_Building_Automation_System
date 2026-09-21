#pragma once

// Turning decoded points into the JSON the page consumes.
//
// Kept separate from both the decoder and the HTTP server so it can be tested
// on its own, and so a second transport later (a websocket push, say) does not
// have to reimplement the encoding.

#include <string>
#include <vector>

#include "../device/read_path.h"
#include "../wire/points.h"

namespace t3000::app
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
    };

    // The whole payload: which device, how it was read (and why), and the points.
    //
    // The read-path decision travels WITH the data deliberately. A grid that is
    // empty because the firmware predates the PTP tunnel must be able to say so
    // on the page; that is the failure this project exists to stop repeating.
    std::string build_inputs_json(const DeviceInfo& device,
                                  const device::Decision& decision,
                                  const std::vector<wire::InputPoint>& points);
}
