#include "points_json.h"

#include "../display/device_text.h"
#include "../display/input_text.h"

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
    }

    std::string build_unavailable_inputs_json(int serial_number,
                                              const std::string& address,
                                              const std::string& reason)
    {
        std::string out = "{\"unavailable\":true,\"device\":{";
        append_int(out, "serialNumber", serial_number); out += ',';

        // Present and false, never absent. Absent is what made the page claim
        // the points had been read from hardware.
        out += "\"isFixture\":false,";
        out += "\"readFromWire\":false,";
        append_field(out, "address", address);
        out += "},";

        // The page always renders a read-path band. "none" is a real answer
        // and a more useful one than the "unknown" it shows when the field is
        // missing entirely.
        out += "\"readPath\":{";
        append_field(out, "path", "none"); out += ',';
        append_field(out, "summary", "nothing was read"); out += ',';
        append_field(out, "detail", reason);
        out += "},";

        append_int(out, "count", 0);

        // "inputs", not "points". The page reads data.inputs; a payload using
        // any other name produces an empty grid with no explanation.
        out += ",\"inputs\":[],";
        append_field(out, "message", reason);
        out += '}';
        return out;
    }

    std::string build_inputs_json(const DeviceInfo& device,
                                  const device::Decision& decision,
                                  const std::vector<wire::InputPoint>& points)
    {
        std::string out;
        out.reserve(256 + points.size() * 192);

        out += '{';

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
        append_field(out, "address", device.address);
        out += "},";

        out += "\"readPath\":{";
        append_field(out, "path", path_name(decision.path)); out += ',';
        append_field(out, "summary", decision.summary ? decision.summary : ""); out += ',';
        append_field(out, "detail", decision.detail);
        out += "},";

        // What T3000 decides from the panel's settings, which T5000 does not
        // read yet: how many of the 64 rows a model shows, and a few per-model
        // labels. Said once, here, rather than guessed per row.
        const display::PanelContext panel;
        out += "\"panel\":{\"known\":false,";
        append_field(out, "note",
                     "Every input is shown. T3000 shows fewer on some panel models, and names a "
                     "few inputs by model; both depend on the panel's settings, which T5000 does not "
                     "read yet.");
        out += "},";

        append_int(out, "count", (long)points.size());
        out += ",\"inputs\":[";

        for (size_t i = 0; i < points.size(); i++)
        {
            const wire::InputPoint& p = points[i];
            if (i != 0) out += ',';

            // The columns as T3000 shows them - text, computed here so the
            // self-test covers it - followed by the raw fields they came from.
            const display::InputText t = display::input_text(p, (int)i, panel);

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
}
