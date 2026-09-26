#pragma once

// Reading a device's outputs once it is known whether they can be read:
// the plan is plan_points_read's, in inputs_plan.h, and the read is
// read_outputs_page's, in outputs_read.h. What is here joins them to the
// payload the Outputs page is sent, as inputs_plan.h does for Inputs.

#include <stdint.h>

#include <string>

#include "../bacnet/point_read.h"
#include "../device/registry.h"
#include "inputs_plan.h"
#include "outputs_read.h"

namespace t5000::app
{
    PointsPlan plan_outputs_read(const device::DeviceRecord& device);

    // What the page is sent once a planned read has been carried out: the
    // outputs, or why there are none, with the plan's note and sighting
    // wherever the read ended. The plan's refusals are answered before any
    // read, with build_unavailable_outputs_json.
    std::string outputs_payload(const device::DeviceRecord& device, const PointsPlan& plan,
                                const OutputsPageRead& read);

    // Carries out a plan that can_read, over a transport already open to
    // plan.endpoint: the read, held to the plan's identity, and the payload
    // from it.
    std::string read_planned_outputs(const device::DeviceRecord& device, const PointsPlan& plan,
                                     bacnet::ReadTransport& transport, const bacnet::ReadSettings& settings,
                                     uint8_t& next_invoke_id);
}
