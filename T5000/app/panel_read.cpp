#include "panel_read.h"

#include <chrono>
#include <thread>

namespace t5000::app
{
    namespace
    {
        void add(std::string& note, const std::string& more)
        {
            if (!note.empty())
                note += ' ';
            note += more;
        }

        // Why a read of names came back empty, for a page that then goes on
        // to show its points. read_entities' own text for silence talks about
        // firewalls and offline devices, which is wrong for a device that has
        // just answered the read before.
        std::string why_not(const bacnet::ReadOutcome& o)
        {
            return o.no_answer ? std::string("the device did not answer the request.") : o.error;
        }
    }

    // T3000 pauses between its connect-time reads too (Sleep(50) at
    // BacnetView.cpp:6476, :6570), and these are small controllers.
    void pause_between_reads(const bacnet::ReadSettings& settings)
    {
        if (settings.pause_between_requests_ms > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(settings.pause_between_requests_ms));
    }

    bool read_panel_settings(bacnet::ReadTransport& transport, const bacnet::Endpoint& device,
                             device::ProductClassId product, uint32_t expected_serial, Identity identity,
                             const bacnet::ReadSettings& settings, uint8_t& next_invoke_id,
                             const PageWords& words, PanelRead& panel, std::string& error,
                             int& requests_sent)
    {
        using namespace bacnet;

        const bool must_confirm = identity == Identity::MustConfirm;
        const std::string expected = std::to_string(expected_serial);

        // Said with every refusal of a device known only from the saved list.
        // The settings are the one read it has been sent; nothing follows.
        const std::string scan_first =
            std::string(" Its address comes from the saved list, and another panel may have it now, so nothing "
                        "more was read from it. Scan, and then open ") +
            words.page + " again.";

        const ReadOutcome s = read_entities(transport, device, ReadCommand::Settings, 1, 1,
                                            (uint16_t)wire::kSettingsWireSize, settings, next_invoke_id);
        requests_sent += s.requests_sent;

        if (s.ok && wire::decode_settings(s.entities.data(), s.entities.size(), panel.settings))
        {
            panel.settings_known = true;

            const uint32_t reported = panel.settings.serial_number;
            if (reported != 0 && reported != expected_serial)
            {
                error = "The panel at " + device.text() + " gives its serial number as " +
                        std::to_string(reported) + " in its settings, not " + expected;
                if (must_confirm)
                    error += ", the serial saved for this device." + scan_first;
                else
                    error += ", the serial the scan found at that address. It may have been replaced or "
                             "renumbered since the scan. Nothing more was read from it; scan again.";
                return false;
            }
            if (reported == 0)
            {
                if (must_confirm)
                {
                    error = "The panel at " + device.text() + " gives no serial number in its settings, "
                            "so T5000 cannot confirm it is serial " + expected + "." + scan_first;
                    return false;
                }
                add(panel.note, "The panel's settings carry no serial number, so T5000 could not confirm "
                                "it is the device the scan found.");
            }
        }
        else if (s.no_answer)
        {
            // As T3000: a panel whose settings do not come back is not
            // connected, and nothing more is asked of it.
            error = s.error;
            return false;
        }
        else if (must_confirm)
        {
            // Refused, or a reply that could not be used. Without the
            // settings there is no serial to check, and for this device
            // nothing else vouches for it.
            error = "The panel at " + device.text() + " did not give settings T5000 could use, so it "
                    "cannot confirm it is serial " + expected + ". " +
                    (s.ok ? std::string("The settings could not be decoded.") : s.error) + scan_first;
            return false;
        }
        else
        {
            add(panel.note, s.ok ? std::string("The panel's settings could not be decoded.") : s.error);
            add(panel.note, std::string("T3000 treats a panel that does not give its settings as not connected, and "
                                        "shows nothing. T5000 reads the ") +
                                words.points + " anyway, but without the settings: " + words.without_settings);
            if (product == device::ProductClassId::Esp32T3Series)
            {
                add(panel.note, std::string("An ESP32 T3 on newer firmware can have more than 64 ") + words.points +
                                    ", and says how many in its settings, so only the first 64 were read.");
            }
        }
        return true;
    }

    void read_digital_names(bacnet::ReadTransport& transport, const bacnet::Endpoint& device,
                            const bacnet::ReadSettings& settings, uint8_t& next_invoke_id,
                            PanelRead& panel, int& requests_sent)
    {
        using namespace bacnet;

        const ReadOutcome units = read_entities(transport, device, ReadCommand::CustomUnits, wire::kCustomUnitCount,
                                                wire::kCustomUnitCount, (uint16_t)wire::kCustomUnitWireSize,
                                                settings, next_invoke_id);
        requests_sent += units.requests_sent;
        if (units.ok)
        {
            display::take_digital_ranges(units.entities.data(), units.entities.size(), 0, wire::kCustomUnitCount,
                                         panel.ranges);
        }
        else
        {
            add(panel.note, "The custom digital range names were not read: " + why_not(units));
        }
    }

    void read_analog_tables(bacnet::ReadTransport& transport, const bacnet::Endpoint& device,
                            const bacnet::ReadSettings& settings, uint8_t& next_invoke_id,
                            PanelRead& panel, int& requests_sent)
    {
        using namespace bacnet;

        // T3000 asks for these whether or not the digital names came back
        // (BacnetView.cpp:6472), and for table 4 only once 0-3 have
        // (:6563-6565).
        const ReadOutcome first_four = read_entities_from(transport, device, ReadCommand::AnalogCustomTables, 0, 4, 4,
                                                          (uint16_t)wire::kAnalogTableWireSize, settings, next_invoke_id);
        requests_sent += first_four.requests_sent;
        if (first_four.ok)
        {
            display::take_analog_tables(first_four.entities.data(), first_four.entities.size(), 0, 4, panel.ranges);

            pause_between_reads(settings);
            const ReadOutcome fifth = read_entities_from(transport, device, ReadCommand::AnalogCustomTables, 4, 1, 1,
                                                         (uint16_t)wire::kAnalogTableWireSize, settings, next_invoke_id);
            requests_sent += fifth.requests_sent;
            if (fifth.ok)
                display::take_analog_tables(fifth.entities.data(), fifth.entities.size(), 4, 1, panel.ranges);
            else
                add(panel.note, "Custom analog table 5's name was not read: " + why_not(fifth));
        }
        else
        {
            add(panel.note, "The custom analog table names were not read: " + why_not(first_four));
        }
    }
}
