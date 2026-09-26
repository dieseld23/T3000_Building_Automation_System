#include "points_json.h"

#include "../bacnet/point_read.h"
#include "../device/input_rows.h"
#include "../device/output_rows.h"
#include "../display/device_text.h"
#include "../display/input_text.h"
#include "../display/output_text.h"

#include <windows.h>

#include <stdio.h>
#include <string.h>

namespace t5000::app
{
    std::string acp_to_utf8(const uint8_t* text, size_t max_length)
    {
        // The device pads with NULs and does not promise a terminator in a full
        // field, so the length is bounded by the field rather than by strlen.
        return display::acp_to_utf8(text, max_length);
    }

    std::string json_escape(const std::string& utf8)
    {
        std::string out;
        out.reserve(utf8.size() + 8);

        for (const char c : utf8)
        {
            switch (c)
            {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if ((unsigned char)c < 0x20)
                {
                    char esc[8];
                    snprintf(esc, sizeof(esc), "\\u%04x", (unsigned)(unsigned char)c);
                    out += esc;
                }
                else
                {
                    out += c;   // already UTF-8; multi-byte sequences pass through
                }
                break;
            }
        }
        return out;
    }

    namespace
    {
        const char* path_name(device::ReadPath p)
        {
            switch (p)
            {
            case device::ReadPath::PrivateData:        return "private-data";
            case device::ReadPath::PrivateDataOverPtp: return "private-data-over-ptp";
            case device::ReadPath::ModbusRegisters:    return "modbus-registers";
            }
            return "unknown";
        }

        void append_field(std::string& out, const char* name, const std::string& value)
        {
            out += '"';
            out += name;
            out += "\":\"";
            out += json_escape(value);
            out += '"';
        }

        void append_int(std::string& out, const char* name, long value)
        {
            char buf[64];
            snprintf(buf, sizeof(buf), "\"%s\":%ld", name, value);
            out += buf;
        }

        void add_note(std::string& note, const std::string& more)
        {
            if (!note.empty())
                note += ' ';
            note += more;
        }

        void append_bool(std::string& out, const char* name, bool value)
        {
            out += '"';
            out += name;
            out += "\":";
            out += value ? "true" : "false";
        }

        // "device":{...}, - the same for every points payload.
        void append_device(std::string& out, const DeviceInfo& device)
        {
            out += "\"device\":{";
            append_int(out, "serialNumber", device.serial_number); out += ',';
            append_int(out, "productId",    device.product_id);    out += ',';
            append_int(out, "firmware",     device.firmware);      out += ',';
            append_int(out, "protocol",     device.protocol);      out += ',';
            out += "\"isFixture\":";
            out += device.is_fixture ? "true" : "false";
            out += ",\"readFromWire\":";
            out += device.read_from_wire ? "true" : "false";
            out += ',';
            append_field(out, "address", device.address); out += ',';
            append_field(out, "sighting", device.sighting);
            out += "},";
        }

        // "readPath":{...},
        void append_read_path(std::string& out, const device::Decision& decision)
        {
            out += "\"readPath\":{";
            append_field(out, "path", path_name(decision.path)); out += ',';
            append_field(out, "summary", decision.summary ? decision.summary : ""); out += ',';
            append_field(out, "detail", decision.detail);
            out += "},";
        }

        // The panel as its settings name it, each field followed by a comma.
        void append_panel_identity(std::string& out, const wire::PanelSettings& settings)
        {
            append_field(out, "name", acp_to_utf8(settings.panel_name, wire::settings_at::panel_name_length)); out += ',';
            append_int(out, "number",       settings.panel_number); out += ',';
            append_int(out, "miniType",     settings.mini_type()); out += ',';
            // As T3000 shows it: "%d.%d" (BacnetSetting.cpp:1194).
            append_field(out, "firmware", std::to_string(settings.firmware_main) + "." +
                                              std::to_string(settings.firmware_sub)); out += ',';
            append_int(out, "serialNumber", (long)settings.serial_number); out += ',';
        }

        // "customRanges":{"digitalKnown":...,"digital":[...]
        // - without the closing brace, so a caller can add the analog names.
        void append_digital_ranges(std::string& out, const display::CustomRanges& ranges)
        {
            out += "\"customRanges\":{\"digitalKnown\":";
            out += ranges.digital_known ? "true" : "false";
            out += ",\"digital\":[";
            for (int i = 0; ranges.digital_known && i < wire::kCustomUnitCount; i++)
            {
                if (i != 0) out += ',';
                out += '"' + json_escape(ranges.digital[i].text) + '"';
            }
            out += ']';
        }

        // A device found but not read: every key the page reads, with nothing
        // known. `points` names the list and its counts - "inputs" gives
        // inputsRead, inputsShown and inputs.
        std::string unavailable_json(int serial_number, const std::string& address, const std::string& reason,
                                     const std::string& sighting, const std::string& points, bool analog_tables)
        {
            std::string out = "{\"unavailable\":true,\"device\":{";
            append_int(out, "serialNumber", serial_number); out += ',';

            // Present and false, never absent. Absent is what made the page claim
            // the points had been read from hardware.
            out += "\"isFixture\":false,";
            out += "\"readFromWire\":false,";
            append_field(out, "address", address); out += ',';
            append_field(out, "sighting", sighting);
            out += "},";

            // The page always renders a read-path band. "none" is a real answer
            // and a more useful one than the "unknown" it shows when the field is
            // missing entirely.
            out += "\"readPath\":{";
            append_field(out, "path", "none"); out += ',';
            append_field(out, "summary", "nothing was read"); out += ',';
            append_field(out, "detail", reason);
            out += "},";

            // The same panel and customRanges keys the full payload writes,
            // with nothing known, so the page never reads a missing field as
            // a value.
            out += "\"panel\":{\"known\":false,\"" + points + "Read\":0,\"" + points + "Shown\":0,\"note\":\"\"},";
            out += "\"customRanges\":{\"digitalKnown\":false,\"digital\":[]";
            if (analog_tables)
                out += ",\"analog\":[]";
            out += "},";

            append_int(out, "count", 0);

            // The list under the page's own name. The Inputs page reads
            // data.inputs; a payload using any other name produces an empty
            // grid with no explanation.
            out += ",\"" + points + "\":[],";
            append_field(out, "message", reason);
            out += '}';
            return out;
        }
    }

    std::string build_unavailable_inputs_json(int serial_number,
                                              const std::string& address,
                                              const std::string& reason,
                                              const std::string& sighting)
    {
        return unavailable_json(serial_number, address, reason, sighting, "inputs", true);
    }

    std::string build_unavailable_outputs_json(int serial_number,
                                               const std::string& address,
                                               const std::string& reason,
                                               const std::string& sighting)
    {
        return unavailable_json(serial_number, address, reason, sighting, "outputs", false);
    }

    std::string build_inputs_json(const DeviceInfo& device,
                                  const device::Decision& decision,
                                  const std::vector<wire::InputPoint>& points,
                                  const InputsPanel& panel)
    {
        std::string out;
        out.reserve(256 + points.size() * 192);

        out += '{';
        append_device(out, device);
        append_read_path(out, decision);

        // What T3000 decides from the panel's settings: how many rows a model
        // shows, and a few per-model labels. Said once, here, rather than per
        // row.
        display::PanelContext context;
        context.known  = panel.known;
        context.type   = static_cast<device::MiniType>(panel.known ? panel.settings.mini_type() : 0);
        context.ranges = panel.ranges;

        size_t shown = points.size();
        std::string note = panel.note;
        if (panel.known)
        {
            const int type = device::bacnet_device_type(panel.settings);
            const device::InputRows rows = device::input_rows(panel.product, panel.settings);
            if (!rows.set)
            {
                add_note(note, "T3000 sets no row count for this panel's model (mini_type " + std::to_string(type) +
                                   ", a Tiny-EX Minipanel): it keeps the count of the panel opened before it, "
                                   "and shows every row empty if there was none. Every input read is shown here.");
            }
            else if ((size_t)rows.rows < points.size())
            {
                shown = (size_t)rows.rows;
                add_note(note, "This panel's model (mini_type " + std::to_string(type) + ") has " +
                                   std::to_string(rows.rows) + " inputs. T3000 shows rows " +
                                   std::to_string(rows.rows + 1) + "-" + std::to_string(points.size()) +
                                   " empty, and they are left out here.");
            }
            if (points.size() > (size_t)bacnet::kInputCount)
            {
                add_note(note, "Its settings give it " + std::to_string(points.size()) +
                                   " inputs, which an ESP32 T3 on firmware 63.7 or later may have, so all " +
                                   std::to_string(points.size()) + " were read.");
            }
        }
        else if (note.empty())
        {
            note = "The panel's settings were not read, so every input is shown. T3000 shows fewer on "
                   "some panel models, and names a few inputs by model.";
        }

        out += "\"panel\":{\"known\":";
        out += panel.known ? "true," : "false,";
        if (panel.known)
            append_panel_identity(out, panel.settings);
        append_int(out, "inputsRead",  (long)points.size()); out += ',';
        append_int(out, "inputsShown", (long)shown); out += ',';
        append_field(out, "note", note);
        out += "},";

        // The custom range names as the device sent them, for reference; the
        // rows below already use them.
        append_digital_ranges(out, panel.ranges);
        out += ",\"analog\":[";
        for (int i = 0; i < wire::kAnalogTableCount; i++)
        {
            if (i != 0) out += ',';
            out += "{\"known\":";
            out += panel.ranges.analog_known[i] ? "true," : "false,";
            append_field(out, "name", panel.ranges.analog[i]);
            out += '}';
        }
        out += "]},";

        append_int(out, "count", (long)shown);
        out += ",\"inputs\":[";

        for (size_t i = 0; i < shown; i++)
        {
            const wire::InputPoint& p = points[i];
            if (i != 0) out += ',';

            // The columns as T3000 shows them - text, computed here so the
            // self-test covers it - followed by the raw fields they came from.
            const display::InputText t = display::input_text(p, (int)i, context);

            out += '{';
            append_int(out, "index", (long)i); out += ',';
            append_int(out, "input", (long)i + 1); out += ',';
            append_field(out, "fullLabel", acp_to_utf8(p.description, wire::kDescriptionLength)); out += ',';
            append_field(out, "label",     acp_to_utf8(p.label,       wire::kLabelLength)); out += ',';
            append_field(out, "autoManual",  t.auto_manual); out += ',';
            append_field(out, "value",       t.value); out += ',';
            append_field(out, "units",       t.units); out += ',';
            append_field(out, "range",       t.range); out += ',';
            append_field(out, "calibration", t.calibration); out += ',';
            append_field(out, "sign",        t.sign); out += ',';
            append_field(out, "filter",      t.filter); out += ',';
            append_field(out, "status",      t.status); out += ',';
            out += "\"alarm\":";
            out += t.alarm ? "true" : "false";
            out += ',';
            append_field(out, "signalType",  t.signal_type); out += ',';
            append_field(out, "note",        t.note); out += ',';

            out += "\"raw\":{";
            append_int(out, "value",         p.value); out += ',';
            append_int(out, "range",         p.range); out += ',';
            append_int(out, "digitalAnalog", p.digital_analog); out += ',';
            append_int(out, "control",       p.control); out += ',';
            append_int(out, "decom",         p.decom); out += ',';
            append_int(out, "subId",         p.sub_id); out += ',';
            append_int(out, "subProduct",    p.sub_product); out += ',';
            append_int(out, "subNumber",     p.sub_number);
            out += "}}";
        }
        out += "]}";
        return out;
    }

    std::string build_outputs_json(const DeviceInfo& device,
                                   const device::Decision& decision,
                                   const std::vector<wire::OutputPoint>& points,
                                   const OutputsPanel& panel)
    {
        std::string out;
        out.reserve(256 + points.size() * 224);

        out += '{';
        append_device(out, device);
        append_read_path(out, decision);

        // What T3000 decides from the panel's settings: how many rows a model
        // shows, which outputs have a hand-off-auto switch, and whether any
        // can be external. Said once, here, rather than per row.
        display::OutputPanel context;
        context.known  = panel.known;
        context.ranges = panel.ranges;

        size_t shown = points.size();
        std::string note = panel.note;
        if (panel.known)
        {
            context.type = device::bacnet_device_type(panel.settings);
            context.rows = device::output_rows(panel.product, panel.settings);

            if ((size_t)context.rows.rows < points.size())
            {
                shown = (size_t)context.rows.rows;
                if (shown == 0)
                {
                    add_note(note, "This panel's model (mini_type " + std::to_string(context.type) +
                                       ") has no outputs. T3000 shows all " + std::to_string(points.size()) +
                                       " rows empty, and they are left out here.");
                }
                else
                {
                    add_note(note, "This panel's model (mini_type " + std::to_string(context.type) + ") has " +
                                       std::to_string(shown) + " outputs. T3000 shows rows " +
                                       std::to_string(shown + 1) + "-" + std::to_string(points.size()) +
                                       " empty, and they are left out here.");
                }
            }
            if (points.size() > (size_t)bacnet::kOutputCount)
            {
                add_note(note, "Its settings give it " + std::to_string(points.size()) +
                                   " outputs, which an ESP32 T3 on firmware 63.7 or later may have, so all " +
                                   std::to_string(points.size()) + " were read.");
            }
        }
        else if (note.empty())
        {
            note = "The panel's settings were not read, so every output is shown. T3000 shows fewer on "
                   "some panel models, and a hand-off-auto switch on some.";
        }

        out += "\"panel\":{";
        append_bool(out, "known", panel.known); out += ',';
        if (panel.known)
            append_panel_identity(out, panel.settings);
        append_int(out, "outputsRead",  (long)points.size()); out += ',';
        append_int(out, "outputsShown", (long)shown); out += ',';
        append_field(out, "note", note);
        out += "},";

        // The custom digital range names as the device sent them, for
        // reference; the rows below already use them.
        append_digital_ranges(out, panel.ranges);
        out += "},";

        append_int(out, "count", (long)shown);
        out += ",\"outputs\":[";
        for (size_t i = 0; i < shown; i++)
        {
            const wire::OutputPoint& p = points[i];
            if (i != 0) out += ',';

            // The columns as T3000 shows them, then the raw fields.
            const display::OutputText t = display::output_text(p, (int)i, context);

            out += '{';
            append_int(out, "index", (long)i); out += ',';
            append_int(out, "output", (long)i + 1); out += ',';
            append_field(out, "fullLabel",    t.full_label); out += ',';
            append_field(out, "label",        t.label); out += ',';
            append_field(out, "autoManual",   t.auto_manual); out += ',';
            append_field(out, "hoa",          t.hoa); out += ',';
            append_bool(out, "hand",          t.hand); out += ',';
            append_field(out, "value",        t.value); out += ',';
            append_field(out, "units",        t.units); out += ',';
            append_field(out, "range",        t.range); out += ',';
            append_field(out, "lowVoltage",   t.low_voltage); out += ',';
            append_field(out, "highVoltage",  t.high_voltage); out += ',';
            append_field(out, "pwmPeriod",    t.pwm_period); out += ',';
            append_field(out, "status",       t.status); out += ',';
            append_bool(out, "external",      t.external); out += ',';
            append_field(out, "productOutput", t.product_output); out += ',';
            append_field(out, "note",         t.note); out += ',';

            out += "\"raw\":{";
            append_int(out, "value",          p.value); out += ',';
            append_int(out, "range",          p.range); out += ',';
            append_int(out, "digitalAnalog",  p.digital_analog); out += ',';
            append_int(out, "control",        p.control); out += ',';
            append_int(out, "autoManual",     p.auto_manual); out += ',';
            append_int(out, "hwSwitch",       p.hw_switch_status); out += ',';
            append_int(out, "digitalControl", p.digital_control); out += ',';
            append_int(out, "decom",          p.decom); out += ',';
            append_int(out, "subId",          p.sub_id); out += ',';
            append_int(out, "subProduct",     p.sub_product); out += ',';
            append_int(out, "subNumber",      p.sub_number);
            out += "}}";
        }
        out += "]}";
        return out;
    }
}
