#pragma once

// Deciding whether a device's Inputs can be read, and from where, before
// anything is sent.
//
// Separate from the read itself so every refusal can be tested without a
// socket, and so the rule that decides whether a request reaches a controller
// at all sits in one place that says why.

#include <string>

#include "../bacnet/point_read.h"
#include "../device/read_path.h"
#include "../device/registry.h"

namespace t5000::app
{
    // PROTOCOL_BACNET_IP, global_define.h:254 - what T3000 sets g_protocol to
    // for a device found by the UDP scan (MainFrm.cpp:7601).
    inline constexpr int kProtocolBacnetIp = 3;

    struct InputsPlan
    {
        bool can_read = false;

        bacnet::Endpoint endpoint;
        device::Decision decision;

        // When !can_read: why, in a sentence for the page.
        std::string reason;

        // When can_read: a limit of the read the operator should know about.
        // Empty for most devices.
        std::string note;
    };

    InputsPlan plan_inputs_read(const device::DeviceRecord& device);
}
