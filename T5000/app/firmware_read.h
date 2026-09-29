#pragma once
// The one request the Firmware page can send: a panel's settings, for its
// bootloader's version.
//
//   1. the panel's settings                 READ_SETTING_COMMAND        1 request
//
// Sent only when the operator asks, for one device, and held to the plan the
// points pages are held to (plan_points_read): a device they would not read
// is sent nothing. What the settings give is kept through keep_bootloader,
// so only settings that give the device's own serial count. Nothing else is
// read, and nothing is written. Checking a file sends nothing at all
// (app/firmware_page.h).

#include <stdint.h>

#include <string>

#include "firmware_page.h"
#include "inputs_plan.h"
#include "../bacnet/point_read.h"
#include "../device/registry.h"

namespace t5000::app
{
    // Whether the page may read this device's settings, from where, and why
    // not: the points pages' plan, in words about settings.
    PointsPlan plan_firmware_read(const device::DeviceRecord& d);

    // GET /api/firmware: every device in the list, as the page lists it,
    // and the largest file the page may send.
    std::string firmware_list_json(const device::Registry& registry);

    // POST /api/firmware/read's body: {"handle":"<n>"}, and nothing else.
    bool read_firmware_read_request(const std::string& body, device::Handle& handle, std::string& message);

    // Carries out a plan that can_read, over a transport already open to
    // plan.endpoint: the settings, and the bootloader's version from them
    // kept. True when it was kept; `message` says what happened either way.
    bool read_bootloader(device::Registry& registry, const device::DeviceRecord& d, const PointsPlan& plan,
                         bacnet::ReadTransport& transport, const bacnet::ReadSettings& settings,
                         uint8_t& next_invoke_id, std::string& message);
}
