#pragma once

// Deciding whether a device's Inputs can be read, and from where, before
// anything is sent.
//
// Separate from the read itself so every refusal can be tested without a
// socket, and so the rule that decides whether a request reaches a controller
// at all sits in one place that says why.

#include <stdint.h>

#include <string>

#include "../bacnet/point_read.h"
#include "../device/read_path.h"
#include "../device/registry.h"
#include "inputs_read.h"

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

        // Whether the device has answered a scan since T5000 started. Strict
        // unless that is shown: a record that says nothing about it is
        // treated as known only from the saved list.
        bool     seen_this_session = false;
        Identity identity          = Identity::MustConfirm;

        // For the page when !seen_this_session: that it has not been seen
        // since T5000 started, and when it last was. Empty otherwise.
        std::string sighting;
    };

    InputsPlan plan_inputs_read(const device::DeviceRecord& device);

    // A time as the page shows it, in this computer's time zone:
    // "2026-09-20 14:03". Empty for 0, which means never.
    std::string local_time_text(int64_t unix_seconds);

    // What the page is sent once a planned read has been carried out: the
    // inputs, or why there are none, with the plan's note and sighting
    // wherever the read ended. The plan's refusals are answered before any
    // read, with build_unavailable_inputs_json.
    std::string inputs_payload(const device::DeviceRecord& device, const InputsPlan& plan,
                               const InputsPageRead& read);
}
