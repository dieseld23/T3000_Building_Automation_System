#include "variables_read.h"

#include "../device/variable_rows.h"

namespace t5000::app
{
    namespace
    {
        const PageWords kVariablesWords = {
            "Variables",
            "variables",
            "multi-state tables 1-3 are asked for, as on firmware 60.7 and older.",
            bacnet::kVariableCount,
        };
    }

    VariablesPageRead read_variables_page(bacnet::ReadTransport& transport, const bacnet::Endpoint& device,
                                          device::ProductClassId product, uint32_t expected_serial,
                                          Identity identity, const bacnet::ReadSettings& settings,
                                          uint8_t& next_invoke_id)
    {
        using namespace bacnet;

        VariablesPageRead r;
        PanelRead& panel = r.panel;

        // 1. The settings, and 2. the custom digital range names.
        if (!read_panel_settings(transport, device, product, expected_serial, identity, settings, next_invoke_id,
                                 kVariablesWords, panel, r.error, r.requests_sent))
            return r;

        pause_between_reads(settings);
        read_digital_names(transport, device, settings, next_invoke_id, panel, r.requests_sent);

        // 3. The multi-state tables, and 4. the custom units.
        pause_between_reads(settings);
        read_msv_tables(transport, device, settings, next_invoke_id, panel, r.names, r.requests_sent);

        pause_between_reads(settings);
        read_variable_units(transport, device, settings, next_invoke_id, panel, r.names, r.requests_sent);

        // 5. The variables.
        pause_between_reads(settings);
        const int count = panel.settings_known ? device::variables_to_read(product, panel.settings) : kVariableCount;
        const VariablesRead variables = read_variables(transport, device, settings, next_invoke_id, count);
        r.requests_sent += variables.transfer.requests_sent;

        if (!variables.ok)
        {
            r.error = variables.error;

            // As for inputs: read_entities' text for silence lists causes
            // the settings reply has just ruled out.
            if (variables.transfer.no_answer && variables.transfer.replies_accepted == 0 && panel.settings_known)
            {
                r.error = "No answer from " + device.text() + " to the request for its variables, though it had "
                          "just answered the request for its settings. It may have gone offline since.";
            }
            return r;
        }

        r.points = variables.points;
        r.ok     = true;
        return r;
    }
}
