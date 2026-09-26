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

    // The Variables page's sample: 128 variables, as a panel has, the first
    // few set up to show each way T3000 shows one - a number and its units,
    // a state pair, a time, a multi-state name, the device's own units and
    // its own state names - and the rest as a panel leaves them. On the
    // same sample panel as Outputs, with its names read.
    DeviceInfo                       fixture_variables_device();
    VariablesPanel                   fixture_variables_panel();
    std::vector<wire::VariablePoint> fixture_variables();
}
