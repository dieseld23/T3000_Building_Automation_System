// Tests for reading the Inputs page - the order of the reads, what each
// failure does to the ones after it - and for the payload built from it.
// Driven by a scripted panel whose answer to each read can be set.

#include "inputs_read.h"
#include "points_json.h"
#include "../testing/check.h"
#include "../testing/fake_transport.h"

#include <string.h>

namespace
{
    using namespace t5000::app;
    using namespace t5000::testing;
    using t5000::bacnet::ReadCommand;
    using t5000::device::ProductClassId;
    namespace w = t5000::wire;

    constexpr uint32_t kSerial = 134341184;

    enum class Does
    {
        Answer,
        Refuse,
        Silent,
        Garble,   // answers, with fewer bytes than the request asked for
    };

    // A panel, scripted per read.
    struct Panel
    {
        Does settings = Does::Answer;
        Does units    = Does::Answer;
        Does tables   = Does::Answer;   // tables 0-3
        Does table4   = Does::Answer;
        Does inputs   = Does::Answer;

        uint8_t  mini_type = 0;
        uint32_t serial    = kSerial;
        int      firmware  = 600;
        uint8_t  max_in    = 0;

        Bytes settings_block() const
        {
            Bytes b(w::kSettingsWireSize, 0);
            b[w::settings_at::mini_type]     = mini_type;
            b[w::settings_at::firmware_main] = (uint8_t)(firmware / 10);
            b[w::settings_at::firmware_sub]  = (uint8_t)(firmware % 10);
            memcpy(&b[w::settings_at::panel_name], "AHU \"2\"", 7);
            b[w::settings_at::panel_number] = 5;
            for (int i = 0; i < 4; i++)
                b[w::settings_at::serial_number + i] = (uint8_t)(serial >> (8 * i));
            b[w::settings_at::max_in] = max_in;
            return b;
        }

        static Bytes units_block()
        {
            Bytes b((size_t)w::kCustomUnitCount * w::kCustomUnitWireSize, 0);
            memcpy(&b[w::custom_unit_at::off], "Closed", 6);
            memcpy(&b[w::custom_unit_at::on], "Tripped", 7);
            return b;
        }

        static Bytes tables_block(int count)
        {
            Bytes b((size_t)count * w::kAnalogTableWireSize, 0);
            memcpy(&b[0], "kPa", 3);
            return b;
        }

        static Bytes inputs_block(const t5000::bacnet::ReadRequest& r)
        {
            Bytes b((size_t)r.count() * 46, 0);
            for (int i = 0; i < r.count(); i++)
            {
                const std::string label = "IN" + std::to_string(r.first + i);
                memcpy(&b[(size_t)i * 46 + 21], label.data(), label.size());
                b[(size_t)i * 46 + 40] = 0;    // digital_analog: digital
                b[(size_t)i * 46 + 45] = 23;   // range 23: custom digital range 1
                b[(size_t)i * 46 + 38] = 1;    // control: on
            }
            return b;
        }

        void respond(const FakeTransport::Sent& s, FakeTransport& t) const
        {
            const auto& r = s.request;
            Does what = Does::Answer;
            Bytes body;
            switch (r.command)
            {
            case ReadCommand::Settings:
                what = settings;
                body = settings_block();
                break;
            case ReadCommand::CustomUnits:
                what = units;
                body = units_block();
                break;
            case ReadCommand::AnalogCustomTables:
                what = r.first == 4 ? table4 : tables;
                body = tables_block(r.count());
                break;
            case ReadCommand::Inputs:
                what = inputs;
                body = inputs_block(r);
                break;
            }

            if (what == Does::Garble)
                body.resize(body.size() / 4);

            if (what == Does::Answer || what == Does::Garble)
                t.reply(ack(r, s.invoke_id, body));
            else if (what == Does::Refuse)
                t.reply(refusal(s.invoke_id));
        }
    };

    struct Run
    {
        FakeTransport  transport;
        InputsPageRead read;
    };

    // A device a scan found at that address this session, unless `identity`
    // says otherwise.
    void run(const Panel& panel, Run& out, ProductClassId product = ProductClassId::Cm5,
             Identity identity = Identity::VouchedForByScan)
    {
        out.transport.respond = [&panel](const FakeTransport::Sent& s, size_t, FakeTransport& t)
        {
            panel.respond(s, t);
        };
        uint8_t invoke = 0;
        out.read = read_inputs_page(out.transport, device_at(), product, kSerial, identity, instant(), invoke);
    }

    // A device known only from the saved list: no scan has found it since
    // T5000 started.
    void run_restored(const Panel& panel, Run& out)
    {
        run(panel, out, ProductClassId::Cm5, Identity::MustConfirm);
    }

    // The commands sent, in order, one letter each: S settings, U units,
    // T tables, I inputs.
    std::string sequence(const FakeTransport& t)
    {
        std::string s;
        for (const auto& sent : t.sent)
        {
            switch (sent.request.command)
            {
            case ReadCommand::Settings:           s += 'S'; break;
            case ReadCommand::CustomUnits:        s += 'U'; break;
            case ReadCommand::AnalogCustomTables: s += 'T'; break;
            case ReadCommand::Inputs:             s += 'I'; break;
            }
        }
        return s;
    }

    bool has(const std::string& text, const char* part)
    {
        return text.find(part) != std::string::npos;
    }

    // ---------------------------------------------------------------------

    void test_everything_answers()
    {
        section("a panel that answers everything: settings, names, then inputs, in T3000's order");

        Panel p;
        p.mini_type = 0x80 | 44;
        Run r;
        run(p, r);

        check(r.read.ok, "the page is read");
        check(sequence(r.transport) == "SUTTIIIIIII", "settings, units, tables 0-3, table 4, seven inputs");
        if (!require(r.transport.sent.size() == 11, "eleven requests"))
            return;
        check(r.transport.sent[0].request.entity_size == 400, "settings: one 400-byte block");
        check(r.transport.sent[1].request.first == 0 && r.transport.sent[1].request.last == 7 &&
                  r.transport.sent[1].request.entity_size == 25,
              "units: 0-7, 25 bytes each");
        check(r.transport.sent[2].request.first == 0 && r.transport.sent[2].request.last == 3 &&
                  r.transport.sent[3].request.first == 4 && r.transport.sent[3].request.last == 4 &&
                  r.transport.sent[3].request.entity_size == 105,
              "tables: 0-3, then 4, 105 bytes each");
        check_eq(r.read.requests_sent, 11, "and counted");

        check(r.read.panel.settings_known, "the settings are known");
        check_eq(r.read.panel.settings.mini_type(), 44, "mini_type, without the chip bits");
        check(r.read.panel.ranges.digital_known && r.read.panel.ranges.digital[0].text == "Closed/Tripped",
              "the custom digital names are known");
        check(r.read.panel.ranges.analog_known[4] && r.read.panel.ranges.analog[0] == "kPa",
              "all five tables are known");
        check(r.read.panel.note.empty(), "nothing to explain");
        check_eq((long)r.read.points.size(), 64, "64 inputs");
    }

    void test_a_different_serial_stops_the_read()
    {
        section("a panel whose settings give another serial is not read further");

        Panel p;
        p.serial = 999;
        Run r;
        run(p, r);

        check(!r.read.ok, "nothing is shown");
        check(sequence(r.transport) == "S", "and nothing more is asked for");
        check(has(r.read.error, "999") && has(r.read.error, std::to_string(kSerial).c_str()),
              "the error gives both serials");
        check(has(r.read.error, "scan again"), "and says what to do");
        check(!has(r.read.error, "saved list"), "a device the scan found is not said to come from the saved list");
    }

    void test_a_zero_serial_is_read_but_said()
    {
        section("a panel whose settings carry no serial is read, and the page says it was not confirmed");

        Panel p;
        p.serial = 0;
        Run r;
        run(p, r);

        check(r.read.ok, "the page is read");
        check(has(r.read.panel.note, "no serial number"), "and says the identity was not confirmed");
    }

    void test_silent_settings_end_the_read()
    {
        section("a panel that does not answer for its settings is not read, as in T3000");

        Panel p;
        p.settings = Does::Silent;
        Run r;
        run(p, r);

        check(!r.read.ok, "nothing is shown");
        check(sequence(r.transport) == "SS", "the settings are asked for twice, and nothing else");
        check(has(r.read.error, "the panel's settings"), "the error says what went unanswered");
    }

    void test_refused_settings_are_noted_and_the_inputs_read()
    {
        section("a panel that refuses its settings still has its inputs read, and the page says how it differs");

        Panel p;
        p.settings = Does::Refuse;
        Run r;
        run(p, r);

        check(r.read.ok, "the page is read");
        check(!r.read.panel.settings_known, "without the settings");
        check(sequence(r.transport) == "SUTTIIIIIII", "the names and inputs are still asked for");
        check(has(r.read.panel.note, "refused"), "the note gives the refusal");
        check(has(r.read.panel.note, "T3000 treats"), "and says T3000 would have shown nothing");
        check(!has(r.read.panel.note, "ESP32"), "a CM5 is not told about ESP32 input counts");

        Run esp32;
        run(p, esp32, ProductClassId::Esp32T3Series);
        check(has(esp32.read.panel.note, "only the first 64"), "an ESP32 is told only 64 were read");
    }

    void test_esp32_reads_its_own_count()
    {
        section("an ESP32 T3 on 63.7 or later is read to its settings' max_in");

        Panel p;
        p.firmware = 637;
        p.max_in   = 96;
        Run r;
        run(p, r, ProductClassId::Esp32T3Series);

        check(r.read.ok, "the page is read");
        check_eq((long)r.read.points.size(), 96, "96 inputs");
        check(sequence(r.transport) == "SUTTIIIIIIIIII", "in ten requests");
    }

    void test_names_that_do_not_come_back()
    {
        section("custom names that do not come back leave a note, and the inputs are still read");

        {
            Panel p;
            p.units = Does::Refuse;
            Run r;
            run(p, r);
            check(r.read.ok && !r.read.panel.ranges.digital_known, "units refused: read, without the names");
            check(sequence(r.transport) == "SUTTIIIIIII", "  the tables are still asked for");
            check(has(r.read.panel.note, "digital range names were not read"), "  and the note says so");
        }
        {
            Panel p;
            p.units = Does::Silent;
            Run r;
            run(p, r);
            check(r.read.ok, "units unanswered: read");
            check(sequence(r.transport) == "SUUTTIIIIIII", "  the tables are still asked for, as T3000 does");
            check(r.read.panel.ranges.analog_known[0] && r.read.panel.ranges.analog_known[4],
                  "  and their names are used");
            check(has(r.read.panel.note, "did not answer"), "  the note says it did not answer");
            check(!has(r.read.panel.note, "firewall"), "  without the advice for a device that never answered");
            check(!has(r.read.panel.note, "analog"), "  and says nothing of the tables, which came back");
        }
        {
            Panel p;
            p.units  = Does::Silent;
            p.tables = Does::Silent;
            Run r;
            run(p, r);
            check(r.read.ok, "units and tables unanswered: read");
            check(sequence(r.transport) == "SUUTTIIIIIII", "  tables 0-3 are asked for, and 4 is not");
            check(!r.read.panel.ranges.analog_known[0], "  without the table names");
            check(has(r.read.panel.note, "analog table names were not read"), "  and the note says so");
        }
        {
            Panel p;
            p.tables = Does::Refuse;
            Run r;
            run(p, r);
            check(r.read.ok, "tables 0-3 refused: read");
            check(sequence(r.transport) == "SUTIIIIIII", "  table 4 is not asked for, as in T3000");
            check(!r.read.panel.ranges.analog_known[0] && !r.read.panel.ranges.analog_known[4],
                  "  and no table is known");
        }
        {
            Panel p;
            p.table4 = Does::Refuse;
            Run r;
            run(p, r);
            check(r.read.ok, "table 4 refused: read");
            check(r.read.panel.ranges.analog_known[3] && !r.read.panel.ranges.analog_known[4],
                  "  tables 1-4 known, 5 not");
            check(has(r.read.panel.note, "table 5"), "  and the note names it");
        }
    }

    void test_silent_inputs_after_answered_settings()
    {
        section("inputs that go unanswered after the settings were answered say so");

        Panel p;
        p.inputs = Does::Silent;
        Run r;
        run(p, r);

        check(!r.read.ok, "nothing is shown");
        check(has(r.read.error, "just answered"), "the error says the settings had just been answered");
        check(!has(r.read.error, "firewall"), "not that nothing ever answered");
    }

    // ---------------------------------------- a device from the saved list

    void test_settings_that_cannot_be_used_are_noted_for_a_scanned_device()
    {
        section("settings that come back short are noted, and the inputs read, for a device the scan found");

        Panel p;
        p.settings = Does::Garble;
        Run r;
        run(p, r);

        check(r.read.ok, "the page is read");
        check(!r.read.panel.settings_known, "without the settings");
        check(sequence(r.transport) == "SUTTIIIIIII", "the names and inputs are still asked for");
        check(has(r.read.panel.note, "T3000 treats"), "and the note says T3000 would have shown nothing");
    }

    void test_a_restored_device_is_read_once_its_serial_is_confirmed()
    {
        section("a device from the saved list whose settings give its saved serial is read in full");

        Panel p;
        Run r;
        run_restored(p, r);

        check(r.read.ok, "the page is read");
        check(sequence(r.transport) == "SUTTIIIIIII", "settings, units, tables, inputs, as for any other");
        check(r.read.panel.settings_known, "with the settings");
        check(r.read.panel.note.empty(), "and nothing to explain");
    }

    void test_a_restored_device_whose_serial_cannot_be_confirmed_is_not_read()
    {
        section("a device from the saved list whose serial cannot be confirmed has nothing more read");

        const std::string serial = std::to_string(kSerial);
        {
            Panel p;
            p.settings = Does::Refuse;
            Run r;
            run_restored(p, r);
            check(!r.read.ok, "settings refused: nothing is shown");
            check(sequence(r.transport) == "S", "  and nothing more is asked for");
            check_eq(r.read.requests_sent, 1, "  one request in all");
            check(has(r.read.error, "refused"), "  the error gives the refusal");
            check(has(r.read.error, serial.c_str()), "  and the serial it could not confirm");
            check(has(r.read.error, "saved list") && has(r.read.error, "Scan"),
                  "  and says why, and to scan first");
        }
        {
            Panel p;
            p.settings = Does::Garble;
            Run r;
            run_restored(p, r);
            check(!r.read.ok, "settings that come back short: nothing is shown");
            check(sequence(r.transport) == "S", "  and nothing more is asked for");
            check_eq(r.read.requests_sent, 1, "  one request in all");
            check(has(r.read.error, "does not match"), "  the error says what was wrong with the reply");
            check(has(r.read.error, "Scan"), "  and to scan first");
        }
        {
            Panel p;
            p.serial = 0;
            Run r;
            run_restored(p, r);
            check(!r.read.ok, "settings with no serial: nothing is shown");
            check(sequence(r.transport) == "S", "  and nothing more is asked for");
            check(has(r.read.error, "no serial number") && has(r.read.error, serial.c_str()),
                  "  the error says there was none to check against the saved one");
            check(has(r.read.error, "Scan"), "  and to scan first");
        }
        {
            Panel p;
            p.serial = 999;
            Run r;
            run_restored(p, r);
            check(!r.read.ok, "another serial: nothing is shown");
            check(sequence(r.transport) == "S", "  and nothing more is asked for");
            check(has(r.read.error, "999") && has(r.read.error, "saved for this device"),
                  "  the error gives both serials, the expected one as the saved one");
            check(!has(r.read.error, "the scan found"), "  not as one a scan found");
            check(has(r.read.error, "Scan"), "  and says to scan first");
        }
        {
            Panel p;
            p.settings = Does::Silent;
            Run r;
            run_restored(p, r);
            check(!r.read.ok, "settings unanswered: nothing is shown");
            check(sequence(r.transport) == "SS", "  asked for twice, as for any other device");
            check(has(r.read.error, "No answer"), "  the error is the one for silence");
            check(!has(r.read.error, "did not give settings"), "  not the one for a reply that could not be used");
        }
    }

    // ------------------------------------------------------------ the payload

    std::vector<w::InputPoint> digital_inputs(int n)
    {
        std::vector<w::InputPoint> points((size_t)n);
        for (auto& p : points)
        {
            p.digital_analog = 0;
            p.range          = 23;
            p.control        = 1;
        }
        return points;
    }

    InputsPanel known_panel(uint8_t mini_type)
    {
        InputsPanel panel;
        panel.known                  = true;
        panel.settings.mini_type_byte = mini_type;
        panel.settings.panel_number   = 5;
        panel.settings.firmware_main  = 60;
        panel.settings.firmware_sub   = 5;
        memcpy(panel.settings.panel_name, "AHU \"2\"", 7);
        panel.product = ProductClassId::Cm5;
        return panel;
    }

    DeviceInfo from_wire()
    {
        DeviceInfo d;
        d.serial_number  = (int)kSerial;
        d.read_from_wire = true;
        return d;
    }

    void test_the_payload_leaves_out_the_rows_t3000_blanks()
    {
        section("the payload has the rows T3000 fills, and says how many it left out");

        const std::string json = build_inputs_json(from_wire(), t5000::device::Decision(), digital_inputs(64),
                                                   known_panel(44));
        check(has(json, "\"count\":8"), "a T3-8AI8AO6DO shows 8");
        check(has(json, "\"inputsRead\":64") && has(json, "\"inputsShown\":8"), "of the 64 read");
        check(has(json, "\"index\":7,") && !has(json, "\"index\":8,"), "rows 0-7, and not 8");
        check(has(json, "rows 9-64 empty"), "the note says which rows T3000 blanks");
        check(has(json, "\"name\":\"AHU \\\"2\\\"\""), "the panel name, escaped");
        check(has(json, "\"miniType\":44") && has(json, "\"number\":5"), "the model code and panel number");
        check(has(json, "\"firmware\":\"60.5\""), "the firmware, as T3000 shows it");

        const std::string all = build_inputs_json(from_wire(), t5000::device::Decision(), digital_inputs(64),
                                                  known_panel(1));
        check(has(all, "\"count\":64"), "a big Minipanel shows all 64");

        const std::string tiny = build_inputs_json(from_wire(), t5000::device::Decision(), digital_inputs(64),
                                                   known_panel(4));
        check(has(tiny, "\"count\":64") && has(tiny, "Tiny-EX"), "a Tiny-EX shows all, and says T3000 sets no count");

        const std::string unknown = build_inputs_json(from_wire(), t5000::device::Decision(), digital_inputs(64));
        check(has(unknown, "\"known\":false") && has(unknown, "\"count\":64"),
              "an unread panel: every input, and known false");
        check(has(unknown, "settings were not read"), "  with a note saying why");
    }

    void test_the_payload_uses_the_panel_type()
    {
        section("the rows get the panel's model, for its own labels");

        // IN9 of an RMC1232 is named by position, not by range
        // (BacnetInput.cpp:1066-1076) - which needs the panel's model to
        // reach the rows.
        std::vector<w::InputPoint> points(9);
        for (auto& p : points)
        {
            p.digital_analog = 1;
            p.range          = 11;
        }

        const std::string rmc = build_inputs_json(from_wire(), t5000::device::Decision(), points, known_panel(29));
        check(has(rmc, "\"range\":\"-30V to -65V\""), "an RMC1232's IN9 has its own range name");

        const std::string unknown = build_inputs_json(from_wire(), t5000::device::Decision(), points);
        check(!has(unknown, "-30V to -65V"), "a panel whose model is not known does not");
    }

    void test_the_payload_uses_the_names()
    {
        section("the rows use the custom names the panel sent");

        InputsPanel panel = known_panel(1);
        panel.ranges.digital_known = true;
        panel.ranges.digital[0]    = { "Closed/Tripped", true, "Closed", "Tripped" };

        const std::string json = build_inputs_json(from_wire(), t5000::device::Decision(), digital_inputs(2), panel);
        check(has(json, "\"value\":\"Tripped\""), "range 23, control 1: Tripped");
        check(has(json, "\"range\":\"Closed/Tripped\""), "and the range in full");
        check(has(json, "\"digitalKnown\":true") && has(json, "\"digital\":[\"Closed/Tripped\""),
              "and the names are listed for reference");
    }
}

int run_inputs_read_tests()
{
    test_everything_answers();
    test_a_different_serial_stops_the_read();
    test_a_zero_serial_is_read_but_said();
    test_silent_settings_end_the_read();
    test_refused_settings_are_noted_and_the_inputs_read();
    test_esp32_reads_its_own_count();
    test_names_that_do_not_come_back();
    test_silent_inputs_after_answered_settings();
    test_settings_that_cannot_be_used_are_noted_for_a_scanned_device();
    test_a_restored_device_is_read_once_its_serial_is_confirmed();
    test_a_restored_device_whose_serial_cannot_be_confirmed_is_not_read();
    test_the_payload_leaves_out_the_rows_t3000_blanks();
    test_the_payload_uses_the_panel_type();
    test_the_payload_uses_the_names();
    return 0;
}
