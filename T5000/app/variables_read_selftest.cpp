// Tests for reading the Variables page - the reads it sends, what each
// failure does to the ones after it - for the plan that decides whether it
// is read, and for the payload built from it. Driven by a scripted panel, as
// the Outputs tests are.

#include "variables_plan.h"
#include "variables_read.h"
#include "points_json.h"
#include "../testing/check.h"
#include "../testing/fake_transport.h"

#include <string.h>

#include <set>

namespace
{
    using namespace t5000::app;
    using namespace t5000::testing;
    using t5000::bacnet::ReadCommand;
    using t5000::device::DeviceRecord;
    using t5000::device::ProductClassId;
    namespace w = t5000::wire;

    constexpr uint32_t kSerial = 134341184;

    enum class Does
    {
        Answer,
        Refuse,
        Silent,
    };

    // A panel, scripted per read. Its first multi-state request and its
    // second can be scripted apart.
    struct Panel
    {
        Does settings  = Does::Answer;
        Does units     = Does::Answer;
        Does msv_first = Does::Answer;
        Does msv_then  = Does::Answer;
        Does var_units = Does::Answer;
        Does variables = Does::Answer;

        uint32_t serial   = kSerial;
        int      firmware = 600;
        uint8_t  max_var  = 0;

        Bytes settings_block() const
        {
            Bytes b(w::kSettingsWireSize, 0);
            b[w::settings_at::mini_type]     = 5;   // MINIPANELARM
            b[w::settings_at::firmware_main] = (uint8_t)(firmware / 10);
            b[w::settings_at::firmware_sub]  = (uint8_t)(firmware % 10);
            memcpy(&b[w::settings_at::panel_name], "AHU 2", 5);
            b[w::settings_at::panel_number] = 5;
            for (int i = 0; i < 4; i++)
                b[w::settings_at::serial_number + i] = (uint8_t)(serial >> (8 * i));
            b[w::settings_at::max_var] = max_var;
            return b;
        }

        static Bytes units_block()
        {
            Bytes b((size_t)w::kCustomUnitCount * w::kCustomUnitWireSize, 0);
            memcpy(&b[w::custom_unit_at::off], "Shut", 4);
            memcpy(&b[w::custom_unit_at::on], "Run", 3);
            return b;
        }

        // Table 0 names 0 Off, 1 Cool and 2 Heat; the others nothing.
        static Bytes msv_block(const t5000::bacnet::ReadRequest& r)
        {
            Bytes b((size_t)r.count() * w::kMsvTableWireSize, 0);
            if (r.first == 0)
            {
                const char* const names[] = { "Off", "Cool", "Heat" };
                for (int k = 0; k < 3; k++)
                {
                    uint8_t* item = &b[(size_t)k * w::kMsvItemWireSize];
                    item[w::msv_item_at::status] = 1;
                    memcpy(item + w::msv_item_at::name, names[k], strlen(names[k]));
                    item[w::msv_item_at::value] = (uint8_t)k;
                }
            }
            return b;
        }

        static Bytes var_units_block()
        {
            Bytes b((size_t)w::kVariableUnitCount * w::kVariableUnitWireSize, 0);
            memcpy(&b[0], "L/s", 3);
            return b;
        }

        // Variable n: label "VAR<n>", digital, on multi-state range 101,
        // naming state n % 3.
        static Bytes variables_block(const t5000::bacnet::ReadRequest& r)
        {
            Bytes b((size_t)r.count() * 39, 0);
            for (int i = 0; i < r.count(); i++)
            {
                uint8_t* e = &b[(size_t)i * 39];
                const std::string label = "VAR" + std::to_string(r.first + i);
                memcpy(e + 21, label.data(), label.size());
                const int32_t value = ((r.first + i) % 3) * 1000;
                e[30] = (uint8_t)(value & 0xFF);
                e[31] = (uint8_t)(value >> 8);
                e[35] = 0;     // digital_analog: digital
                e[38] = 101;   // range 101: multi-state table 1
            }
            return b;
        }

        void respond(const FakeTransport::Sent& s, FakeTransport& t) const
        {
            const auto& r = s.request;
            Does what = Does::Refuse;
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
            case ReadCommand::MsvTables:
                what = r.first == 0 ? msv_first : msv_then;
                body = msv_block(r);
                break;
            case ReadCommand::VariableUnits:
                what = var_units;
                body = var_units_block();
                break;
            case ReadCommand::Variables:
                what = variables;
                body = variables_block(r);
                break;
            default:
                break;   // anything else is refused: the page should not ask
            }

            if (what == Does::Answer)
                t.reply(ack(r, s.invoke_id, body));
            else if (what == Does::Refuse)
                t.reply(refusal(s.invoke_id));
        }
    };

    struct Run
    {
        FakeTransport     transport;
        VariablesPageRead read;
    };

    void run(const Panel& panel, Run& out, ProductClassId product = ProductClassId::MiniPanelArm,
             Identity identity = Identity::VouchedForByScan)
    {
        out.transport.respond = [&panel](const FakeTransport::Sent& s, size_t, FakeTransport& t)
        {
            panel.respond(s, t);
        };
        uint8_t invoke = 0;
        out.read = read_variables_page(out.transport, device_at(), product, kSerial, identity, instant(), invoke);
    }

    int sent(const Run& r, ReadCommand c)
    {
        int n = 0;
        for (const auto& s : r.transport.sent)
            n += s.request.command == c ? 1 : 0;
        return n;
    }

    // The multi-state requests, as "first-last" each.
    std::string msv_requests(const Run& r)
    {
        std::string out;
        for (const auto& s : r.transport.sent)
        {
            if (s.request.command != ReadCommand::MsvTables)
                continue;
            if (!out.empty())
                out += ' ';
            out += std::to_string(s.request.first) + "-" + std::to_string(s.request.last);
        }
        return out;
    }

    bool has(const std::string& text, const std::string& part)
    {
        return text.find(part) != std::string::npos;
    }

    // ------------------------------------------------------------ the read

    void test_everything_answers()
    {
        section("the Variables page reads the settings, the names, the tables, the units and the variables");

        const Panel panel;
        Run r;
        run(panel, r);

        check(r.read.ok, "the read succeeds");
        check_eq((long)r.read.points.size(), 128, "128 variables");
        check(r.read.panel.settings_known && r.read.panel.ranges.digital_known, "the settings and names are known");
        check(r.read.names.msv_known() && r.read.names.unit_read[0], "the tables and units too");
        check(r.read.panel.note.empty(), "with nothing to say about them");

        if (!require(r.transport.sent.size() == 18, "eighteen requests"))
            return;
        const auto& s = r.transport.sent;
        check(s[0].request.command == ReadCommand::Settings, "the settings first");
        check(s[1].request.command == ReadCommand::CustomUnits, "then the digital names");
        check(s[2].request.command == ReadCommand::MsvTables && s[3].request.command == ReadCommand::MsvTables,
              "then the multi-state tables, in two requests");
        check(s[4].request.command == ReadCommand::VariableUnits, "then the custom units");
        check(s[4].request.first == 0 && s[4].request.last == 4 && s[4].request.entity_size == 20,
              "  0-4, of 20 bytes");
        check_eq(sent(r, ReadCommand::Variables), 13, "then the variables");
        check(msv_requests(r) == "0-1 2-2", "firmware 60.0: tables 0-1, then 2");
        check(s[2].request.entity_size == 184, "  of 184 bytes");
        check_eq(sent(r, ReadCommand::AnalogCustomTables), 0, "and never the analog table names");
        check_eq(sent(r, ReadCommand::Inputs) + sent(r, ReadCommand::Outputs), 0, "or inputs or outputs");
        check_eq(r.read.requests_sent, 18, "and it counts them");
    }

    void test_which_tables_by_firmware()
    {
        section("which multi-state tables, by firmware: 60.7 and older 0-1 and 2, newer 0-1 and 2-3");

        Panel panel;
        panel.firmware = 607;
        Run old;
        run(panel, old);
        check(msv_requests(old) == "0-1 2-2", "60.7: 0-1, then 2");
        check(old.read.names.msv_known(), "  and that is all of them");

        panel.firmware = 608;
        Run newer;
        run(panel, newer);
        check(msv_requests(newer) == "0-1 2-3", "60.8: 0-1, then 2-3");
        check(newer.read.names.msv_known() && newer.read.names.msv[3].read, "  and table 3 is read");
    }

    void test_a_table_request_that_fails()
    {
        section("a multi-state request that fails is a note, and the next is still sent");

        Panel panel;
        panel.msv_first = Does::Refuse;
        Run r;
        run(panel, r);
        check(r.read.ok, "the variables are still read");
        check(msv_requests(r) == "0-1 2-2", "both requests sent, as T3000 sends them");
        check(!r.read.names.msv[0].read && r.read.names.msv[2].read, "tables 0-1 missing, 2 read");
        check(!r.read.names.msv_known(), "  so not every table is known");
        check(has(r.read.panel.note, "Multi-state tables 1-2 were not read"), "  and the note says which");

        panel.msv_first = Does::Answer;
        panel.msv_then  = Does::Silent;
        Run quiet;
        run(panel, quiet);
        check(quiet.read.ok, "a table request that goes unanswered does not stop the page");
        check(has(quiet.read.panel.note, "Multi-state table 3 was not read: the device did not answer"),
              "  and the note says table 3 went unanswered");
        check_eq(sent(quiet, ReadCommand::VariableUnits), 1, "  the units are still asked for");
    }

    void test_units_that_do_not_come_back()
    {
        section("custom units that do not come back are a note, not a stop");

        Panel panel;
        panel.var_units = Does::Refuse;
        Run r;
        run(panel, r);
        check(r.read.ok, "the variables are still read");
        check(!r.read.names.unit_read[0], "  without the units");
        check(has(r.read.panel.note, "custom variable units were not read"), "  and the note says so");
    }

    void test_a_different_serial_stops_the_read()
    {
        section("a panel whose settings give another serial is not read further");

        Panel panel;
        panel.serial = 999;
        Run r;
        run(panel, r);
        check(!r.read.ok, "not read");
        check_eq((long)r.transport.sent.size(), 1, "  and nothing followed the settings");
    }

    void test_refused_settings()
    {
        section("refused settings: read on for a scanned device, stop for a saved one");

        Panel panel;
        panel.settings = Does::Refuse;
        panel.firmware = 640;   // not known, so not used
        Run r;
        run(panel, r);
        check(r.read.ok, "a device the scan vouched for is read");
        check(has(r.read.panel.note, "T5000 reads the variables anyway") &&
                  has(r.read.panel.note, "multi-state tables 1-3 are asked for"),
              "  and the note says what that means for variables");
        check(msv_requests(r) == "0-1 2-2", "  tables 0-1 and 2, as on older firmware");
        check_eq((long)r.read.points.size(), 128, "  128 variables");

        Run esp;
        run(panel, esp, ProductClassId::Esp32T3Series);
        check(has(esp.read.panel.note, "more than 128 variables") && has(esp.read.panel.note, "first 128"),
              "an ESP32 T3 is told it may have more than 128");

        Run saved;
        run(panel, saved, ProductClassId::MiniPanelArm, Identity::MustConfirm);
        check(!saved.read.ok && has(saved.read.error, "Scan, and then open Variables again"),
              "a device from the saved list is not read, and is told to scan");
        check_eq((long)saved.transport.sent.size(), 1, "  after the settings alone");
    }

    void test_silent_settings_end_the_read()
    {
        section("settings that do not come back end the read, as in T3000");

        Panel panel;
        panel.settings = Does::Silent;
        Run r;
        run(panel, r);
        check(!r.read.ok, "not read");
        check_eq(sent(r, ReadCommand::Settings), (long)r.transport.sent.size(), "  only the settings were asked for");
    }

    void test_silent_variables_after_answered_settings()
    {
        section("variables that do not come back after the settings did");

        Panel panel;
        panel.variables = Does::Silent;
        Run r;
        run(panel, r);
        check(!r.read.ok, "not read");
        check(has(r.read.error, "request for its variables, though it had just answered"),
              "  and says the settings had just answered");
        check(r.read.points.empty(), "  with no points");
    }

    void test_esp32_reads_its_own_count()
    {
        section("an ESP32 T3 on 63.7 reads as many variables as its settings say");

        Panel panel;
        panel.firmware = 637;
        panel.max_var  = 200;
        Run r;
        run(panel, r, ProductClassId::Esp32T3Series);
        check(r.read.ok, "read");
        check_eq((long)r.read.points.size(), 200, "200 variables");
        check_eq(sent(r, ReadCommand::Variables), 20, "in twenty requests");

        panel.firmware = 636;
        Run older;
        run(panel, older, ProductClassId::Esp32T3Series);
        check_eq((long)older.read.points.size(), 128, "on 63.6: 128");
    }

    // --------------------------------------------------------- the payload

    DeviceRecord scanned(ProductClassId product = ProductClassId::MiniPanelArm)
    {
        DeviceRecord d;
        d.serial_number        = kSerial;
        d.product              = product;
        d.firmware             = 600;
        d.connection.transport = t5000::device::Transport::BacnetIp;
        d.connection.host      = "192.168.1.50";
        d.connection.udp_port  = 47808;
        d.provenance           = t5000::device::Provenance::BacnetBroadcast;
        d.answered_scan        = 1;
        d.last_seen            = 1790000000;
        return d;
    }

    std::string payload_for(const Panel& panel, ProductClassId product = ProductClassId::MiniPanelArm)
    {
        const DeviceRecord d = scanned(product);
        const PointsPlan plan = plan_variables_read(d);
        FakeTransport t;
        t.respond = [&panel](const FakeTransport::Sent& s, size_t, FakeTransport& tr)
        {
            panel.respond(s, tr);
        };
        uint8_t invoke = 0;
        return read_planned_variables(d, plan, t, instant(), invoke);
    }

    void test_the_payload()
    {
        section("the payload: T3000's text for each variable");

        const Panel panel;
        const std::string json = payload_for(panel);
        check(has(json, "\"readFromWire\":true"), "read from the wire, and says so");
        check(has(json, "\"variablesRead\":128,\"variablesShown\":128"), "all 128 shown");
        check(has(json, "\"variable\":1,\"fullLabel\":\"\",\"autoManual\":\"Auto\",\"value\":\"Off\","
                        "\"units\":\"Off / Cool / Heat /...\",\"label\":\"VAR0\",\"note\":\"\""),
              "variable 1 is point 0, named by table 1, exactly as T3000 shows it");
        check(has(json, "\"variable\":3,\"fullLabel\":\"\",\"autoManual\":\"Auto\",\"value\":\"Heat\""),
              "  and variable 3 is Heat");
        check(has(json, "\"units\":[\"L/s\",\"\",\"\",\"\",\"\"]"), "the custom units, for reference");
        check(has(json, "\"msvKnown\":true,\"msv\":[{\"table\":1,\"range\":\"Off / Cool / Heat /...\"},"
                        "{\"table\":2,\"range\":\"\"},{\"table\":3,\"range\":\"\"}]"),
              "  and the tables that came back");
        check(has(json, "\"count\":128,\"variables\":["), "the list under the page's name");
    }

    // Every "key": in a JSON text, however deep.
    std::set<std::string> keys_of(const std::string& json)
    {
        std::set<std::string> keys;
        size_t i = 0;
        while (i < json.size())
        {
            if (json[i] != '"')
            {
                i++;
                continue;
            }
            size_t end = i + 1;
            while (end < json.size() && json[end] != '"')
                end += json[end] == '\\' ? 2 : 1;
            if (end + 1 < json.size() && json[end + 1] == ':')
                keys.insert(json.substr(i + 1, end - i - 1));
            i = end + 1;
        }
        return keys;
    }

    std::set<std::string> minus(const std::set<std::string>& a, const std::set<std::string>& b)
    {
        std::set<std::string> out;
        for (const auto& k : a)
            if (!b.count(k))
                out.insert(k);
        return out;
    }

    void test_the_unavailable_payload_has_every_key()
    {
        section("a device that was not read gets every key a read one does");

        // A read panel with no rows and no tables, so neither payload has row
        // or table keys.
        VariablesPanel known;
        known.known = true;
        DeviceInfo info;
        const std::set<std::string> read = keys_of(build_variables_json(info, t5000::device::Decision(), {}, known));
        const std::set<std::string> unread = keys_of(build_unavailable_variables_json(1, "a", "why", "when"));

        const std::set<std::string> expected_only_read = {
            "productId", "firmware", "protocol", "name", "number", "miniType",
        };
        check(minus(read, unread) == expected_only_read,
              "only what the page does not read for an unread device is missing from it");
        check(minus(unread, read) == std::set<std::string>({ "unavailable", "message" }),
              "and it adds only unavailable and message");
        check(unread.count("variables") && unread.count("variablesRead") && unread.count("units") &&
                  unread.count("msvKnown") && unread.count("msv"),
              "  variables, variablesRead, units, msvKnown and msv among them");

        // With tables, only each table's own keys are added.
        const std::set<std::string> with_tables = keys_of(payload_for(Panel()));
        const std::set<std::string> table_and_row = minus(minus(with_tables, read), unread);
        check(table_and_row == std::set<std::string>({ "table", "range", "index", "variable", "fullLabel",
                                                       "autoManual", "value", "label", "raw", "digitalAnalog",
                                                       "control" }),
              "  a read with rows and tables adds only row and table keys");
    }

    void test_the_other_pages_unavailable_payloads_did_not_change()
    {
        section("the Inputs and Outputs payloads for an unread device are as they were");

        check(build_unavailable_inputs_json(7, "10.0.0.1:47808", "why", "when") ==
                  "{\"unavailable\":true,\"device\":{\"serialNumber\":7,\"isFixture\":false,"
                  "\"readFromWire\":false,\"address\":\"10.0.0.1:47808\",\"sighting\":\"when\"},"
                  "\"readPath\":{\"path\":\"none\",\"summary\":\"nothing was read\",\"detail\":\"why\"},"
                  "\"panel\":{\"known\":false,\"inputsRead\":0,\"inputsShown\":0,\"note\":\"\"},"
                  "\"customRanges\":{\"digitalKnown\":false,\"digital\":[],\"analog\":[]},"
                  "\"count\":0,\"inputs\":[],\"message\":\"why\"}",
              "Inputs, byte for byte");
        check(build_unavailable_outputs_json(7, "10.0.0.1:47808", "why", "when") ==
                  "{\"unavailable\":true,\"device\":{\"serialNumber\":7,\"isFixture\":false,"
                  "\"readFromWire\":false,\"address\":\"10.0.0.1:47808\",\"sighting\":\"when\"},"
                  "\"readPath\":{\"path\":\"none\",\"summary\":\"nothing was read\",\"detail\":\"why\"},"
                  "\"panel\":{\"known\":false,\"outputsRead\":0,\"outputsShown\":0,\"note\":\"\"},"
                  "\"customRanges\":{\"digitalKnown\":false,\"digital\":[]},"
                  "\"count\":0,\"outputs\":[],\"message\":\"why\"}",
              "Outputs, byte for byte");
    }

    // ------------------------------------------------------------ the plan

    void test_the_plan()
    {
        section("whether a device's variables are read, in the Variables page's words");

        check(plan_variables_read(scanned()).can_read, "a scanned panel is read");

        DeviceRecord child = scanned(ProductClassId::Tstat10);
        child.parent_serial = 500001;
        const PointsPlan behind = plan_variables_read(child);
        check(!behind.can_read && has(behind.reason, "controller's variables"),
              "a device behind a controller is not, and the reason says variables");

        DeviceRecord serial = scanned();
        serial.connection.transport = t5000::device::Transport::ModbusRtu;
        check(has(plan_variables_read(serial).reason, "T5000 reads variables over BACnet/IP only so far"),
              "a serial device: variables over BACnet/IP only");

        DeviceRecord by_hand = scanned();
        by_hand.provenance = t5000::device::Provenance::ManuallyAdded;
        check(!plan_variables_read(by_hand).can_read, "a device added by hand is not read");
    }

    void test_a_restored_device_whose_serial_is_confirmed()
    {
        section("a device from the saved list whose settings give its serial is read, and the page says so");

        DeviceRecord saved = scanned();
        saved.provenance    = t5000::device::Provenance::Restored;
        saved.answered_scan = 0;

        const Panel panel;
        FakeTransport t;
        t.respond = [&panel](const FakeTransport::Sent& s, size_t, FakeTransport& tr)
        {
            panel.respond(s, tr);
        };
        uint8_t invoke = 0;
        const std::string json = read_planned_variables(saved, plan_variables_read(saved), t, instant(), invoke);
        check(has(json, "\"readFromWire\":true"), "read");
        check(has(json, "Not seen since T5000 started"), "  with the sighting");
        check(has(json, "Its settings give serial " + std::to_string(kSerial) + ", the one saved for it"),
              "  and that its settings confirmed it is the same device");
    }

    void test_the_read_is_held_to_the_plans_identity()
    {
        section("a planned Variables read is held to the plan's identity");

        DeviceRecord saved = scanned();
        saved.provenance    = t5000::device::Provenance::Restored;
        saved.answered_scan = 0;

        FakeTransport t;
        t.respond = [](const FakeTransport::Sent& s, size_t, FakeTransport& tr)
        {
            tr.reply(refusal(s.invoke_id));
        };
        uint8_t invoke = 0;
        const std::string json = read_planned_variables(saved, plan_variables_read(saved), t, instant(), invoke);
        check_eq((long)t.sent.size(), 1, "from the saved list: the settings, and nothing after");
        check(has(json, "\"unavailable\":true") && has(json, "Scan, and then open Variables again"),
              "  and the page is told to scan first");
        check(has(json, "\"variables\":[]"), "  in the Variables page's shape");
    }
}

int run_variables_read_tests()
{
    test_everything_answers();
    test_which_tables_by_firmware();
    test_a_table_request_that_fails();
    test_units_that_do_not_come_back();
    test_a_different_serial_stops_the_read();
    test_refused_settings();
    test_silent_settings_end_the_read();
    test_silent_variables_after_answered_settings();
    test_esp32_reads_its_own_count();
    test_the_payload();
    test_the_unavailable_payload_has_every_key();
    test_the_other_pages_unavailable_payloads_did_not_change();
    test_the_plan();
    test_a_restored_device_whose_serial_is_confirmed();
    test_the_read_is_held_to_the_plans_identity();
    return 0;
}
