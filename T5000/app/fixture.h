#pragma once

// Canned points, so the page can be worked on without a controller on the desk.
//
// Everything served from here is flagged isFixture in the JSON and the page
// shows a warning band. Sample data that looks like device data is worse than
// no data at all - it is the one thing in a configuration tool that must never
// be ambiguous.

#include <vector>

#include "../wire/points.h"
#include "points_json.h"

namespace t5000::app
{
    std::vector<wire::InputPoint> fixture_points();

    // The Outputs page's sample: a panel whose settings are "known" - a
    // Tiny MiniPanel, whose first eight outputs have hand-off-auto switches -
    // so the switch column, the rows it marks and an external output all
    // appear when the page is worked on. Flagged isFixture like the rest.
    DeviceInfo                     fixture_outputs_device();
    OutputsPanel                   fixture_outputs_panel();
    std::vector<wire::OutputPoint> fixture_outputs();
}
