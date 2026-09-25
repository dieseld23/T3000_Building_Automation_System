#include "inputs_plan.h"

#include <time.h>

#include "points_json.h"

namespace t5000::app
{
    std::string local_time_text(int64_t unix_seconds)
    {
        if (unix_seconds <= 0)
            return std::string();

        const time_t t = (time_t)unix_seconds;
        struct tm local = {};
        if (localtime_s(&local, &t) != 0)
            return std::string();

        char text[32] = {};
        strftime(text, sizeof(text), "%Y-%m-%d %H:%M", &local);
        return text;
    }

    InputsPlan plan_inputs_read(const device::DeviceRecord& d)
    {
        using device::Transport;

        InputsPlan plan;

        // Decided first, so it is on the plan whatever else is refused.
        // answered_scan is set only by a scan in this session; a device
        // restored from the saved list starts at 0.
        plan.seen_this_session = d.answered_scan != 0;
        plan.identity = plan.seen_this_session ? Identity::VouchedForByScan : Identity::MustConfirm;
        if (!plan.seen_this_session)
        {
            const std::string when = local_time_text(d.last_seen);
            plan.sighting = "Not seen since T5000 started: this device has not answered a scan this "
                            "session, so its address is the one saved " +
                            (when.empty() ? std::string("for it.") : "when it was last seen, " + when + ".");
        }

        if (d.connection.transport != Transport::BacnetIp)
        {
            plan.reason = std::string("T5000 reads inputs over BACnet/IP only so far, and this "
                                      "device is reached over ") +
                          device::transport_name(d.connection.transport) + ".";
            return plan;
        }

        // Before anything else about the device: if another controller
        // answered for it, the address is that controller's, and a read sent
        // there comes back - correctly framed, the right size, passing every
        // check - with the controller's own points.
        if (d.parent_serial != 0)
        {
            plan.reason = "This device is on the RS485 bus of controller " +
                          std::to_string(d.parent_serial) +
                          ", which answered the scan for it, so the address in the scan is "
                          "that controller's. T3000 reads a device like this through its "
                          "controller, over Modbus; T5000 does not yet. A read sent to that "
                          "address would come back with the controller's inputs under this "
                          "device's serial, so none is sent.";
            return plan;
        }

        plan.decision = device::choose_read_path((int)d.product, d.firmware, kProtocolBacnetIp);
        if (plan.decision.path != device::ReadPath::PrivateData)
        {
            plan.reason = plan.decision.detail +
                          " T5000 does not read Modbus registers yet, so nothing was read.";
            return plan;
        }

        if (d.connection.host.empty())
        {
            plan.reason = "The scan did not report an address for this device, so there is "
                          "nowhere to send the request.";
            return plan;
        }
        if (!bacnet::parse_endpoint(d.connection.host, d.connection.udp_port, plan.endpoint))
        {
            plan.reason = "'" + d.connection.host + "' port " + std::to_string(d.connection.udp_port) +
                          " is not an IPv4 address and port T5000 can send to.";
            return plan;
        }

        // How many inputs to read is decided by the read itself, from the
        // panel's settings: 64, or more on an ESP32 T3 on firmware 63.7 or
        // later (app/inputs_read.cpp).
        plan.can_read = true;
        return plan;
    }

    std::string inputs_payload(const device::DeviceRecord& d, const InputsPlan& plan, const InputsPageRead& read)
    {
        if (!read.ok)
            return build_unavailable_inputs_json((int)d.serial_number, d.address_note, read.error, plan.sighting);

        DeviceInfo info;
        info.serial_number  = (int)d.serial_number;
        info.product_id     = (int)static_cast<uint8_t>(d.product);
        info.firmware       = d.firmware;
        info.protocol       = kProtocolBacnetIp;
        info.read_from_wire = true;
        info.address        = plan.endpoint.text();
        info.sighting       = plan.sighting;

        // A read that got this far under MustConfirm had its serial checked:
        // any other outcome of the settings read refuses it. Said, so the
        // note about the saved address does not read as doubt about the data.
        if (!info.sighting.empty() && plan.identity == Identity::MustConfirm)
        {
            info.sighting += " Its settings give serial " + std::to_string(d.serial_number) +
                             ", the one saved for it, so it is the same device.";
        }

        device::Decision decision = plan.decision;
        if (!plan.note.empty())
            decision.detail += " " + plan.note;

        InputsPanel panel;
        panel.known    = read.panel.settings_known;
        panel.settings = read.panel.settings;
        panel.product  = d.product;
        panel.ranges   = read.panel.ranges;
        panel.note     = read.panel.note;

        return build_inputs_json(info, decision, read.points, panel);
    }

    std::string read_planned_inputs(const device::DeviceRecord& d, const InputsPlan& plan,
                                    bacnet::ReadTransport& transport, const bacnet::ReadSettings& settings,
                                    uint8_t& next_invoke_id)
    {
        // The plan says how sure T5000 already is of the panel at the
        // address; the read holds a device known only from the saved list to
        // the stricter rule. See Identity.
        const InputsPageRead read = read_inputs_page(transport, plan.endpoint, d.product, d.serial_number,
                                                     plan.identity, settings, next_invoke_id);
        return inputs_payload(d, plan, read);
    }
}
