#include "outputs_plan.h"

#include "points_json.h"

namespace t5000::app
{
    PointsPlan plan_outputs_read(const device::DeviceRecord& d)
    {
        return plan_points_read(d, "outputs");
    }

    std::string outputs_payload(const device::DeviceRecord& d, const PointsPlan& plan, const OutputsPageRead& read)
    {
        if (!read.ok)
            return build_unavailable_outputs_json((int)d.serial_number, d.address_note, read.error, plan.sighting);

        DeviceInfo info;
        info.serial_number  = (int)d.serial_number;
        info.product_id     = (int)static_cast<uint8_t>(d.product);
        info.firmware       = d.firmware;
        info.protocol       = kProtocolBacnetIp;
        info.read_from_wire = true;
        info.address        = plan.endpoint.text();
        info.sighting       = plan.sighting;

        // As for inputs: a read that got this far under MustConfirm had its
        // serial checked.
        if (!info.sighting.empty() && plan.identity == Identity::MustConfirm)
        {
            info.sighting += " Its settings give serial " + std::to_string(d.serial_number) +
                             ", the one saved for it, so it is the same device.";
        }

        device::Decision decision = plan.decision;
        if (!plan.note.empty())
            decision.detail += " " + plan.note;

        OutputsPanel panel;
        panel.known    = read.panel.settings_known;
        panel.settings = read.panel.settings;
        panel.product  = d.product;
        panel.ranges   = read.panel.ranges;
        panel.note     = read.panel.note;

        return build_outputs_json(info, decision, read.points, panel);
    }

    std::string read_planned_outputs(const device::DeviceRecord& d, const PointsPlan& plan,
                                     bacnet::ReadTransport& transport, const bacnet::ReadSettings& settings,
                                     uint8_t& next_invoke_id)
    {
        const OutputsPageRead read = read_outputs_page(transport, plan.endpoint, d.product, d.serial_number,
                                                       plan.identity, settings, next_invoke_id);
        return outputs_payload(d, plan, read);
    }
}
