#include "inputs_read.h"

#include "../device/input_rows.h"

namespace t5000::app
{
    namespace
    {
        const PageWords kInputsWords = {
            "Inputs",
            "inputs",
            "every input read is shown, and no model's own labels are used.",
        };
    }

    InputsPageRead read_inputs_page(bacnet::ReadTransport& transport, const bacnet::Endpoint& device,
                                    device::ProductClassId product, uint32_t expected_serial,
                                    Identity identity, const bacnet::ReadSettings& settings,
                                    uint8_t& next_invoke_id)
    {
        using namespace bacnet;

        InputsPageRead r;
        PanelRead& panel = r.panel;

        // 1-3, in T3000's order: panel_read.h.
        if (!read_panel_settings(transport, device, product, expected_serial, identity, settings, next_invoke_id,
                                 kInputsWords, panel, r.error, r.requests_sent))
            return r;

        pause_between_reads(settings);
        read_digital_names(transport, device, settings, next_invoke_id, panel, r.requests_sent);

        pause_between_reads(settings);
        read_analog_tables(transport, device, settings, next_invoke_id, panel, r.requests_sent);

        // 4. The inputs.
        pause_between_reads(settings);
        const int count = panel.settings_known ? device::inputs_to_read(product, panel.settings) : kInputCount;
        const InputsRead inputs = read_inputs(transport, device, settings, next_invoke_id, count);
        r.requests_sent += inputs.transfer.requests_sent;

        if (!inputs.ok)
        {
            r.error = inputs.error;

            // read_entities says "nothing answered at all" and lists causes
            // that the settings reply has just ruled out.
            if (inputs.transfer.no_answer && inputs.transfer.replies_accepted == 0 && panel.settings_known)
            {
                r.error = "No answer from " + device.text() + " to the request for its inputs, though it had "
                          "just answered the request for its settings. It may have gone offline since.";
            }
            return r;
        }

        r.points = inputs.points;
        r.ok     = true;
        return r;
    }
}
