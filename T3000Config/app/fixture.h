#pragma once

// Canned points, so the page can be worked on without a controller on the desk.
//
// Everything served from here is flagged isFixture in the JSON and the page
// shows a warning band. Sample data that looks like device data is worse than
// no data at all - it is the one thing in a configuration tool that must never
// be ambiguous.

#include <vector>

#include "../wire/points.h"

namespace t3000::app
{
    std::vector<wire::InputPoint> fixture_points();
}
