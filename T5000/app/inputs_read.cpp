#include "inputs_read.h"

#include <chrono>
#include <thread>

#include "../device/input_rows.h"

namespace t5000::app
{
    namespace
    {
        // Between reads, as between the requests within one: T3000 pauses
        // between its connect-time reads too (Sleep(50) at
        // BacnetView.cpp:6476, :6570), and these are small controllers.
        void pause(const bacnet::ReadSettings& settings)
        {
            if (settings.pause_between_requests_ms > 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(settings.pause_between_requests_ms));
        }

        void add(std::string& note, const std::string& more)
        {
            if (!note.empty())
                note += ' ';
            note += more;
        }

        // Why a read of names came back empty, for a page that then goes on
        // to show the inputs. read_entities' own text for silence talks about
        // firewalls and offline devices, which is wrong for a device that has
        // just answered the read before.
        std::string why_not(const bacnet::ReadOutcome& o)
        {
            return o.no_answer ? std::string("the device did not answer the request.") : o.error;
        }
    }

    InputsPageRead read_inputs_page(bacnet::ReadTransport& transport, const bacnet::Endpoint& device,
                                    device::ProductClassId product, uint32_t scanned_serial,
                                    const bacnet::ReadSettings& settings, uint8_t& next_invoke_id)
    {
        using namespace bacnet;

        InputsPageRead r;
        PanelRead& panel = r.panel;

        // 1. The settings.
        const ReadOutcome s = read_entities(transport, device, ReadCommand::Settings, 1, 1,
                                            (uint16_t)wire::kSettingsWireSize, settings, next_invoke_id);
        r.requests_sent += s.requests_sent;

        if (s.ok && wire::decode_settings(s.entities.data(), s.entities.size(), panel.settings))
        {
            panel.settings_known = true;

            const uint32_t reported = panel.settings.serial_number;
            if (reported != 0 && reported != scanned_serial)
            {
                r.error = "The panel at " + device.text() + " gives its serial number as " +
                          std::to_string(reported) + " in its settings, not " + std::to_string(scanned_serial) +
                          ", the serial the scan found at that address. It may have been replaced or "
                          "renumbered since the scan. Nothing more was read from it; scan again.";
                return r;
            }
            if (reported == 0)
            {
                add(panel.note, "The panel's settings carry no serial number, so T5000 could not confirm "
                                "it is the device the scan found.");
            }
        }
        else if (s.no_answer)
        {
            // As T3000: a panel whose settings do not come back is not
            // connected, and nothing more is asked of it.
            r.error = s.error;
            return r;
        }
        else
        {
            add(panel.note, s.ok ? std::string("The panel's settings could not be decoded.") : s.error);
            add(panel.note, "T3000 treats a panel that does not give its settings as not connected, and "
                            "shows nothing. T5000 reads the inputs anyway, but without the settings: every "
                            "input read is shown, and no model's own labels are used.");
            if (product == device::ProductClassId::Esp32T3Series)
            {
                add(panel.note, "An ESP32 T3 on newer firmware can have more than 64 inputs, and says how "
                                "many in its settings, so only the first 64 were read.");
            }
        }

        // 2. The custom digital range names.
        pause(settings);
        const ReadOutcome units = read_entities(transport, device, ReadCommand::CustomUnits, wire::kCustomUnitCount,
                                                wire::kCustomUnitCount, (uint16_t)wire::kCustomUnitWireSize,
                                                settings, next_invoke_id);
        r.requests_sent += units.requests_sent;
        if (units.ok)
        {
            display::take_digital_ranges(units.entities.data(), units.entities.size(), 0, wire::kCustomUnitCount,
                                         panel.ranges);
        }
        else
        {
            add(panel.note, "The custom digital range names were not read: " + why_not(units));
        }

        // 3. The custom analog table names: 0-3, and then 4 only if 0-3 came
        // back, as T3000 does (BacnetView.cpp:6563-6565). T3000 asks for them
        // whether or not the digital names came back (:6472), so this does too.
        pause(settings);
        const ReadOutcome first_four = read_entities_from(transport, device, ReadCommand::AnalogCustomTables, 0, 4, 4,
                                                          (uint16_t)wire::kAnalogTableWireSize, settings, next_invoke_id);
        r.requests_sent += first_four.requests_sent;

        if (first_four.ok)
        {
            display::take_analog_tables(first_four.entities.data(), first_four.entities.size(), 0, 4, panel.ranges);

            pause(settings);
            const ReadOutcome fifth = read_entities_from(transport, device, ReadCommand::AnalogCustomTables, 4, 1, 1,
                                                         (uint16_t)wire::kAnalogTableWireSize, settings, next_invoke_id);
            r.requests_sent += fifth.requests_sent;
            if (fifth.ok)
                display::take_analog_tables(fifth.entities.data(), fifth.entities.size(), 4, 1, panel.ranges);
            else
                add(panel.note, "Custom analog table 5's name was not read: " + why_not(fifth));
        }
        else
        {
            add(panel.note, "The custom analog table names were not read: " + why_not(first_four));
        }

        // 4. The inputs.
        pause(settings);
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
