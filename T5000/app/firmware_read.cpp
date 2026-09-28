#include "firmware_read.h"

#include <map>

#include "panel_read.h"
#include "../json/read.h"

namespace t5000::app
{
    PointsPlan plan_firmware_read(const device::DeviceRecord& d)
    {
        return plan_points_read(d, "settings");
    }

    std::string firmware_list_json(const device::Registry& registry)
    {
        std::string out = "{\"devices\":[";
        bool first = true;
        for (const auto& d : registry.devices())
        {
            if (!first)
                out += ',';
            first = false;
            const PointsPlan plan = plan_firmware_read(d);
            out += firmware_device_json(d, plan.can_read, plan.reason);
        }
        out += "],\"largestFile\":" + std::to_string(kLargestFirmwareRequest) + "}";
        return out;
    }

    bool read_firmware_read_request(const std::string& body, device::Handle& handle, std::string& message)
    {
        std::map<std::string, json::FlatValue> fields;
        std::string error;
        if (!json::parse_flat_object(body, fields, error))
        {
            message = "The request must be a JSON object: " + error;
            return false;
        }

        unsigned long long raw = 0;
        const auto h = fields.find("handle");
        if (fields.size() != 1 || h == fields.end() || !json::parse_u64(h->second.text, raw) || raw == 0)
        {
            message = "The request must be {\"handle\":\"<n>\"}, with a device's handle as the list gives it.";
            return false;
        }
        handle = device::to_handle(raw);
        return true;
    }

    bool read_bootloader(device::Registry& registry, const device::DeviceRecord& d, const PointsPlan& plan,
                         bacnet::ReadTransport& transport, const bacnet::ReadSettings& settings,
                         uint8_t& next_invoke_id, std::string& message)
    {
        // The words read_panel_settings uses in what it says; the points
        // ones are for a page that reads on, which this does not.
        static const PageWords kWords = { "Firmware", "settings", "", 0 };

        PanelRead panel;
        std::string error;
        int sent = 0;
        if (!read_panel_settings(transport, plan.endpoint, d.product, d.serial_number, plan.identity, settings,
                                 next_invoke_id, kWords, panel, error, sent))
        {
            message = error;
            return false;
        }

        const std::string where = "The panel at " + plan.endpoint.text();
        if (!panel.settings_known)
        {
            message = where + " did not give settings T5000 could use, so its bootloader's version is not known.";
            return false;
        }

        if (panel.settings.bootloader_rev == 0)
        {
            message = where + " gives 0 for its bootloader's version, which a panel that does not report one "
                              "leaves there, so T5000 kept nothing.";
            return false;
        }

        // Only here does the read put anything on the device, and only through
        // the one rule every settings read keeps it by.
        if (!keep_bootloader(registry, d.handle, panel, "the Firmware page"))
        {
            message = where + " gives no serial number in its settings, so T5000 cannot tell they are this "
                              "device's, and kept nothing from them.";
            return false;
        }

        message = "Its settings give bootloader version " + std::to_string((int)panel.settings.bootloader_rev) +
                  ". Nothing else was read.";
        return true;
    }
}
