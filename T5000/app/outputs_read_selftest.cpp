// Tests for reading the Outputs page - the reads it sends, what each failure
// does to the ones after it - for the plan that decides whether it is read,
// and for the payload built from it. Driven by a scripted panel, as the
// Inputs tests are.

#include "outputs_plan.h"
#include "outputs_read.h"
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

    // A panel, scripted per read.
    struct Panel
    {
        Does settings = Does::Answer;
        Does units    = Does::Answer;
        Does outputs  = Does::Answer;

        uint8_t  mini_type = 1;   // BIG_MINIPANEL: 12 digital and 12 analog switched
        uint32_t serial    = kSerial;
        int      firmware  = 600;
        uint8_t  max_out   = 0;

        Bytes settings_block() const
        {
            Bytes b(w::kSettingsWireSize, 0);
            b[w::settings_at::mini_type]     = mini_type;
            b[w::settings_at::firmware_main] = (uint8_t)(firmware / 10);
            b[w::settings_at::firmware_sub]  = (uint8_t)(firmware % 10);
            memcpy(&b[w::settings_at::panel_name], "AHU 2", 5);
            b[w::settings_at::panel_number] = 5;
            for (int i = 0; i < 4; i++)
                b[w::settings_at::serial_number + i] = (uint8_t)(serial >> (8 * i));
            b[w::settings_at::max_out] = max_out;
            return b;
        }

        static Bytes units_block()
        {
            Bytes b((size_t)w::kCustomUnitCount * w::kCustomUnitWireSize, 0);
            memcpy(&b[w::custom_unit_at::off], "Shut", 4);
            memcpy(&b[w::custom_unit_at::on], "Run", 3);
            return b;
        }

        // Output n: label "OUT<n>", digital, on custom range 1 - which needs
        // the names - and at the hand position.
        static Bytes outputs_block(const t5000::bacnet::ReadRequest& r)
        {
            Bytes b((size_t)r.count() * 45, 0);
            for (int i = 0; i < r.count(); i++)
            {
                uint8_t* e = &b[(size_t)i * 45];
                const std::string label = "OUT" + std::to_string(r.first + i);
                memcpy(e + 21, label.data(), label.size());
                e[35] = 0;    // digital_analog: digital
                e[36] = 2;    // hw_switch_status: hand
                e[37] = 1;    // control: on
                e[40] = 23;   // range 23: custom digital range 1
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
            case ReadCommand::Outputs:
                what = outputs;
                body = outputs_block(r);
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
        FakeTransport   transport;
        OutputsPageRead read;
    };

    void run(const Panel& panel, Run& out, ProductClassId product = ProductClassId::MiniPanelArm,
             Identity identity = Identity::VouchedForByScan)
    {
        out.transport.respond = [&panel](const FakeTransport::Sent& s, size_t, FakeTransport& t)
        {
            panel.respond(s, t);
        };
        uint8_t invoke = 0;
        out.read = read_outputs_page(out.transport, device_at(), product, kSerial, identity, instant(), invoke);
    }

    int sent(const Run& r, ReadCommand c)
    {
        int n = 0;
        for (const auto& s : r.transport.sent)
            n += s.request.command == c ? 1 : 0;
        return n;
    }

    bool has(const std::string& text, const std::string& part)
    {
        return text.find(part) != std::string::npos;
    }

    // ------------------------------------------------------------ the read

    void test_everything_answers()
    {
        section("the Outputs page reads the settings, the digital names and the outputs - nothing else");

        const Panel panel;
        Run r;
        run(panel, r);

        check(r.read.ok, "the read succeeds");
        check_eq((long)r.read.points.size(), 64, "64 outputs");
        check(r.read.panel.settings_known && r.read.panel.ranges.digital_known, "the settings and names are known");
        check(r.read.panel.note.empty(), "with nothing to say about them");

        if (!require(r.transport.sent.size() == 9, "nine requests: settings, names, and seven of outputs"))
            return;
        check(r.transport.sent[0].request.command == ReadCommand::Settings, "the settings first");
        check(r.transport.sent[1].request.command == ReadCommand::CustomUnits, "then the digital names");
        check_eq(sent(r, ReadCommand::Outputs), 7, "then the outputs");
        check_eq(sent(r, ReadCommand::AnalogCustomTables), 0, "and never the analog table names");
        check_eq(sent(r, ReadCommand::Inputs), 0, "or the inputs");
        check_eq(r.read.requests_sent, 9, "and it counts them");
    }

    void test_a_different_serial_stops_the_read()
    {
        section("a panel whose settings give another serial is not read further");

        Panel panel;
        panel.serial = 999;
        Run r;
        run(panel, r);
        check(!r.read.ok, "not read");
        check(has(r.read.error, "999") && has(r.read.error, std::to_string(kSerial)), "  names both serials");
        check_eq((long)r.transport.sent.size(), 1, "  and nothing followed the settings");
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

    void test_refused_settings()
    {
        section("refused settings: read on for a scanned device, stop for a saved one");

        Panel panel;
        panel.settings = Does::Refuse;
        Run r;
        run(panel, r);
        check(r.read.ok, "a device the scan vouched for is read");
        check(!r.read.panel.settings_known, "  without the settings");
        check(has(r.read.panel.note, "T5000 reads the outputs anyway") &&
                  has(r.read.panel.note, "no model's hand-off-auto switches"),
              "  and the note says what that means for outputs");
        check_eq((long)r.read.points.size(), 64, "  64 outputs");

        Run esp;
        run(panel, esp, ProductClassId::Esp32T3Series);
        check(has(esp.read.panel.note, "more than 64 outputs"), "an ESP32 T3 is told it may have more than 64");

        Run saved;
        run(panel, saved, ProductClassId::MiniPanelArm, Identity::MustConfirm);
        check(!saved.read.ok, "a device from the saved list is not read");
        check(has(saved.read.error, "Scan, and then open Outputs again"), "  and is told to scan, then open Outputs");
        check_eq((long)saved.transport.sent.size(), 1, "  after the settings alone");
    }

    void test_a_zero_serial()
    {
        section("settings with serial 0");

        Panel panel;
        panel.serial = 0;
        Run r;
        run(panel, r);
        check(r.read.ok && has(r.read.panel.note, "no serial number"), "scanned: read, and said");

        Run saved;
        run(panel, saved, ProductClassId::MiniPanelArm, Identity::MustConfirm);
        check(!saved.read.ok && has(saved.read.error, "open Outputs again"), "saved: not read");
    }

    void test_names_that_do_not_come_back()
    {
        section("digital names that do not come back are a note, not a stop");

        Panel panel;
        panel.units = Does::Refuse;
        Run r;
        run(panel, r);
        check(r.read.ok, "the outputs are still read");
        check(!r.read.panel.ranges.digital_known, "  without the names");
        check(has(r.read.panel.note, "custom digital range names were not read"), "  and the note says so");
    }

    void test_silent_outputs_after_answered_settings()
    {
        section("outputs that do not come back after the settings did");

        Panel panel;
        panel.outputs = Does::Silent;
        Run r;
        run(panel, r);
        check(!r.read.ok, "not read");
        check(has(r.read.error, "request for its outputs, though it had just answered"),
              "  and says the settings had just answered");
        check(r.read.points.empty(), "  with no points");
    }

    void test_esp32_reads_its_own_count()
    {
        section("an ESP32 T3 on 63.7 reads as many outputs as its settings say");

        Panel panel;
        panel.firmware = 637;
        panel.max_out  = 96;
        Run r;
        run(panel, r, ProductClassId::Esp32T3Series);
        check(r.read.ok, "read");
        check_eq((long)r.read.points.size(), 96, "96 outputs");
        check_eq(sent(r, ReadCommand::Outputs), 10, "in ten requests");

        Run older;
        panel.firmware = 636;
        run(panel, older, ProductClassId::Esp32T3Series);
        check_eq((long)older.read.points.size(), 64, "on 63.6: 64");
    }

    // --------------------------------------------------------- the payload

    // A device as the scan found it this session.
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
        const PointsPlan plan = plan_outputs_read(d);
        FakeTransport t;
        t.respond = [&panel](const FakeTransport::Sent& s, size_t, FakeTransport& tr)
        {
            panel.respond(s, tr);
        };
        uint8_t invoke = 0;
        return read_planned_outputs(d, plan, t, instant(), invoke);
    }

    void test_the_payload()
    {
        section("the payload: T3000's text for each output");

        const Panel panel;
        const std::string json = payload_for(panel);
        check(has(json, "\"readFromWire\":true"), "read from the wire, and says so");
        check(has(json, "\"outputsRead\":64,\"outputsShown\":64"), "all 64 shown on a T3-BB");
        check(has(json, "\"output\":1,\"fullLabel\":\"\",\"label\":\"OUT0\""), "output 1 is point 0");
        check(has(json, "\"hoa\":\"MAN-ON\",\"hand\":true"), "a switched output at hand is MAN-ON");
        check(has(json, "\"output\":24,") && has(json, "\"output\":25,"), "outputs 24 and 25 are both there");
        check(has(json, "\"range\":\"Shut/Run\"") && has(json, "\"value\":\"Run\""), "the device's names are used");

        // Output 25 is past the switches: AUTO, though its switch byte says
        // hand, and Auto/Man is written.
        const size_t at = json.find("\"output\":25,");
        check(at != std::string::npos && json.find("\"hoa\":\"AUTO\"", at) == json.find("\"hoa\":", at),
              "output 25 is past the switches: AUTO");
    }

    void test_the_payload_leaves_out_the_rows_t3000_blanks()
    {
        section("the payload leaves out the rows T3000 shows empty");

        Panel t38;
        t38.mini_type = 44;   // PM_T38AI8AO6DO: 14 outputs
        std::string json = payload_for(t38);
        check(has(json, "\"outputsRead\":64,\"outputsShown\":14"), "a T3-8AI8AO6DO shows 14");
        check(has(json, "has 14 outputs. T3000 shows rows 15-64 empty"), "  and says why");
        check(has(json, "\"count\":14,"), "  and counts 14");

        Panel t22;
        t22.mini_type = 43;   // PM_T322AI: none
        json = payload_for(t22);
        check(has(json, "\"outputsShown\":0") && has(json, "\"outputs\":[]"), "a T3-22AI shows none");
        check(has(json, "has no outputs. T3000 shows all 64 rows empty"), "  and says it has none");
        check(!has(json, "\"unavailable\""), "  which is a successful read, not a failed one");
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

        // A read that shows nothing, so neither payload has row keys.
        Panel t22;
        t22.mini_type = 43;
        const std::set<std::string> read = keys_of(payload_for(t22));
        const std::set<std::string> unread = keys_of(build_unavailable_outputs_json(1, "a", "why", "when"));

        const std::set<std::string> only_read = minus(read, unread);
        const std::set<std::string> expected_only_read = {
            "productId", "firmware", "protocol", "name", "number", "miniType",
        };
        check(only_read == expected_only_read,
              "only what the page does not read for an unread device is missing from it");
        const std::set<std::string> only_unread = minus(unread, read);
        check(only_unread == std::set<std::string>({ "unavailable", "message" }),
              "and it adds only unavailable and message");
        check(unread.count("outputs") && unread.count("outputsRead") && unread.count("outputsShown"),
              "  outputs, outputsRead and outputsShown among them");
    }

    void test_the_inputs_payloads_did_not_change()
    {
        section("the Inputs payload for an unread device is as it was");

        const std::string json = build_unavailable_inputs_json(7, "10.0.0.1:47808", "why", "when");
        check(json == "{\"unavailable\":true,\"device\":{\"serialNumber\":7,\"isFixture\":false,"
                      "\"readFromWire\":false,\"address\":\"10.0.0.1:47808\",\"sighting\":\"when\"},"
                      "\"readPath\":{\"path\":\"none\",\"summary\":\"nothing was read\",\"detail\":\"why\"},"
                      "\"panel\":{\"known\":false,\"inputsRead\":0,\"inputsShown\":0,\"note\":\"\"},"
                      "\"customRanges\":{\"digitalKnown\":false,\"digital\":[],\"analog\":[]},"
                      "\"count\":0,\"inputs\":[],\"message\":\"why\"}",
              "byte for byte");
    }

    // ------------------------------------------------------------ the plan

    void test_the_plan()
    {
        section("whether a device's outputs are read, in the Outputs page's words");

        check(plan_outputs_read(scanned()).can_read, "a scanned T3-BB is read");

        DeviceRecord child = scanned(ProductClassId::Tstat10);
        child.parent_serial = 500001;
        const PointsPlan behind = plan_outputs_read(child);
        check(!behind.can_read && has(behind.reason, "controller's outputs"),
              "a device behind a controller is not, and the reason says outputs");
        check(has(plan_inputs_read(child).reason, "controller's inputs"), "  while Inputs still says inputs");

        DeviceRecord serial = scanned();
        serial.connection.transport = t5000::device::Transport::ModbusRtu;
        check(has(plan_outputs_read(serial).reason, "T5000 reads outputs over BACnet/IP only so far"),
              "a serial device: outputs over BACnet/IP only");
        check(has(plan_inputs_read(serial).reason, "T5000 reads inputs over BACnet/IP only so far"),
              "  and inputs, as before");

        DeviceRecord by_hand = scanned();
        by_hand.provenance = t5000::device::Provenance::ManuallyAdded;
        check(!plan_outputs_read(by_hand).can_read, "a device added by hand is not read");
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
        const std::string json = read_planned_outputs(saved, plan_outputs_read(saved), t, instant(), invoke);
        check(has(json, "\"readFromWire\":true"), "read");
        check(has(json, "Not seen since T5000 started"), "  with the sighting");
        check(has(json, "Its settings give serial " + std::to_string(kSerial) + ", the one saved for it"),
              "  and that its settings confirmed it is the same device");
    }

    void test_the_read_is_held_to_the_plans_identity()
    {
        section("a planned Outputs read is held to the plan's identity");

        const auto refuses = [](const FakeTransport::Sent& s, size_t, FakeTransport& t)
        {
            t.reply(refusal(s.invoke_id));
        };
        uint8_t invoke = 0;

        DeviceRecord saved = scanned();
        saved.provenance    = t5000::device::Provenance::Restored;
        saved.answered_scan = 0;

        FakeTransport t;
        t.respond = refuses;
        const std::string json = read_planned_outputs(saved, plan_outputs_read(saved), t, instant(), invoke);
        check_eq((long)t.sent.size(), 1, "from the saved list: the settings, and nothing after");
        check(has(json, "\"unavailable\":true") && has(json, "Scan, and then open Outputs again"),
              "  and the page is told to scan first");
        check(has(json, "Not seen since T5000 started"), "  with the sighting");
        check(has(json, "\"outputs\":[]"), "  in the Outputs page's shape");
    }
}

int run_outputs_read_tests()
{
    test_everything_answers();
    test_a_different_serial_stops_the_read();
    test_silent_settings_end_the_read();
    test_refused_settings();
    test_a_zero_serial();
    test_names_that_do_not_come_back();
    test_silent_outputs_after_answered_settings();
    test_esp32_reads_its_own_count();
    test_the_payload();
    test_the_payload_leaves_out_the_rows_t3000_blanks();
    test_the_unavailable_payload_has_every_key();
    test_the_inputs_payloads_did_not_change();
    test_the_plan();
    test_a_restored_device_whose_serial_is_confirmed();
    test_the_read_is_held_to_the_plans_identity();
    return 0;
}
