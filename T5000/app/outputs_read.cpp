#include "outputs_read.h"

#include "../device/output_rows.h"

namespace t5000::app
{
    namespace
    {
        const PageWords kOutputsWords = {
            "Outputs",
            "outputs",
            "every output read is shown, with no model's hand-off-auto switches, and none shown as external.",
        };
    }

    OutputsPageRead read_outputs_page(bacnet::ReadTransport& transport, const bacnet::Endpoint& device,
                                      device::ProductClassId product, uint32_t expected_serial,
                                      Identity identity, const bacnet::ReadSettings& settings,
                                      uint8_t& next_invoke_id)
    {
        using namespace bacnet;

        OutputsPageRead r;
        PanelRead& panel = r.panel;

        // 1. The settings, and 2. the custom digital range names.
        if (!read_panel_settings(transport, device, product, expected_serial, identity, settings, next_invoke_id,
                                 kOutputsWords, panel, r.error, r.requests_sent))
            return r;

        pause_between_reads(settings);
        read_digital_names(transport, device, settings, next_invoke_id, panel, r.requests_sent);

        // 3. The outputs.
        pause_between_reads(settings);
        const int count = panel.settings_known ? device::outputs_to_read(product, panel.settings) : kOutputCount;
        const OutputsRead outputs = read_outputs(transport, device, settings, next_invoke_id, count);
        r.requests_sent += outputs.transfer.requests_sent;

        if (!outputs.ok)
        {
            r.error = outputs.error;

            // As for inputs: read_entities' text for silence lists causes
            // the settings reply has just ruled out.
            if (outputs.transfer.no_answer && outputs.transfer.replies_accepted == 0 && panel.settings_known)
            {
                r.error = "No answer from " + device.text() + " to the request for its outputs, though it had "
                          "just answered the request for its settings. It may have gone offline since.";
            }
            return r;
        }

        r.points = outputs.points;
        r.ok     = true;
        return r;
    }
}
