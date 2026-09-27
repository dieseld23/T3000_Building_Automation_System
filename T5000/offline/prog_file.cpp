#include "prog_file.h"

#include <stdio.h>
#include <string.h>

#include <algorithm>

#include "../wire/points.h"

namespace t5000::offline
{
    namespace
    {
        // Keeps a text field to its first 0, and zeros after it.
        void keep_text(InputBytes& to, const InputBytes& from, size_t at, size_t length)
        {
            const uint8_t* text = &from[at];
            const size_t used = strnlen(reinterpret_cast<const char*>(text), length);
            memset(&to[at], 0, length);
            memcpy(&to[at], text, used);
        }

        int base64_digit(char c)
        {
            if (c >= 'A' && c <= 'Z')
                return c - 'A';
            if (c >= 'a' && c <= 'z')
                return c - 'a' + 26;
            if (c >= '0' && c <= '9')
                return c - '0' + 52;
            if (c == '+')
                return 62;
            if (c == '/')
                return 63;
            return -1;
        }
    }

    const std::vector<ProgSection>& prog_sections()
    {
        // SaveBacnetBinaryFile, global_function.cpp:12741-12876.
        static const std::vector<ProgSection> sections = {
            { "BAC_INPUT_ITEM_COUNT",                 "Str_in_point",               64,  46,   5 },
            { "BAC_OUTPUT_ITEM_COUNT",                "Str_out_point",              64,  45,   5 },
            { "BAC_VARIABLE_ITEM_COUNT",              "Str_variable_point",         128, 39,   5 },
            { "BAC_PROGRAM_ITEM_COUNT",               "Str_program_point",          16,  37,   5 },
            { "BAC_PID_COUNT",                        "Str_controller_point",       16,  28,   5 },
            { "BAC_SCREEN_COUNT",                     "Control_group_point",        16,  46,   5 },
            { "BAC_GRPHIC_LABEL_COUNT",               "Str_label_point",            240, 70,   5 },
            { "BAC_USER_LOGIN_COUNT",                 "Str_userlogin_point",        8,   48,   5 },
            { "BAC_CUSTOMER_UNITS_COUNT",             "Str_Units_element",          8,   25,   5 },
            { "BAC_ALALOG_CUSTMER_RANGE_TABLE_COUNT", "Str_table_point",            5,   105,  5 },
            { "",                                     "Str_Setting_Info",           1,   400,  5 },
            { "BAC_SCHEDULE_COUNT",                   "Str_weekly_routine_point",   8,   42,   5 },
            { "BAC_HOLIDAY_COUNT",                    "Str_annual_routine_point",   4,   33,   5 },
            { "BAC_MONITOR_COUNT",                    "Str_monitor_point",          12,  104,  5 },
            { "BAC_WEEKLYCODE_ROUTINES_COUNT",        "WEEKLY_SCHEDULE_SIZE",       8,   144,  5 },
            { "BAC_HOLIDAY_COUNT",                    "ANNUAL_CODE_SIZE",           4,   46,   5 },
            { "BAC_PROGRAMCODE_ITEM_COUNT",           "2000",                       16,  2000, 5 },
            { "BAC_VARIABLE_CUS_UNIT_COUNT",          "Str_variable_uint_point",    5,   20,   6 },
            { "BAC_MSV_COUNT",                        "Str_MSV",                    3,   184,  7 },
            { "BAC_SCHEDULE_COUNT",                   "Str_schedual_time_flag",     8,   72,   8 },
        };
        return sections;
    }

    size_t prog_table_at(size_t index)
    {
        size_t at = prog_at::inputs;
        const auto& sections = prog_sections();
        for (size_t i = 0; i < index && i < sections.size(); i++)
            at += (size_t)sections[i].items * sections[i].size;
        return at;
    }

    size_t prog_file_length(int version)
    {
        if (version < kFirstProgVersion || version > kLastProgVersion)
            return 0;
        size_t length = prog_at::inputs;
        for (const auto& s : prog_sections())
            if (s.since <= version)
                length += (size_t)s.items * s.size;
        return length;
    }

    bool read_prog_file(const uint8_t* data, size_t size, ProgFile& out, std::string& why)
    {
        out = ProgFile();

        // T3000 reads anything else as the INI files an older T3000 kept, or
        // fails to (global_function.cpp:10545-10586).
        if (size < prog_at::inputs || data[0] != 0x55 || data[1] != 0xFF || data[prog_at::version] < kFirstProgVersion)
        {
            why = "This is not a .prog file as T3000 saves one today. If an older T3000 saved it, open it in "
                  "T3000 and save it again.";
            return false;
        }

        const int version = data[prog_at::version];
        if (version > kLastProgVersion)
        {
            why = "This .prog file is version " + std::to_string(version) + ", saved by a newer T3000. T5000 reads "
                  "versions " + std::to_string(kFirstProgVersion) + " to " + std::to_string(kLastProgVersion) + ".";
            return false;
        }

        const size_t expected = prog_file_length(version);
        if (size != expected)
        {
            why = "This .prog file is " + std::to_string(size) + " bytes, and a version " + std::to_string(version) +
                  " file is " + std::to_string(expected) + ". It may have been cut short, or changed since T3000 "
                  "saved it.";
            return false;
        }

        wire::PanelSettings settings;
        if (!wire::decode_settings(data + prog_at::settings, wire::kSettingsWireSize, settings))
        {
            why = "The panel settings in this .prog file could not be read.";
            return false;
        }

        out.version  = version;
        out.settings = settings;
        for (int i = 0; i < kProgInputs; i++)
        {
            InputBytes p;
            memcpy(p.data(), data + prog_at::inputs + (size_t)i * p.size(), p.size());
            out.inputs.push_back(p);
        }
        return true;
    }

    InputBytes imported_input(int index, const InputBytes& from_file)
    {
        InputBytes p = default_input(index);

        keep_text(p, from_file, input_at::description, wire::kDescriptionLength);
        keep_text(p, from_file, input_at::label, wire::kLabelLength);

        p[input_at::filter]           = from_file[input_at::filter];
        p[input_at::auto_manual]      = from_file[input_at::auto_manual];
        p[input_at::digital_analog]   = from_file[input_at::digital_analog];
        p[input_at::range]            = from_file[input_at::range];
        p[input_at::calibration_sign] = from_file[input_at::calibration_sign];
        p[input_at::calibration_h]    = from_file[input_at::calibration_h];
        p[input_at::calibration_l]    = from_file[input_at::calibration_l];

        // The signal type; the status beside it is the panel's.
        p[input_at::decom] = (uint8_t)((p[input_at::decom] & 0x0F) | (from_file[input_at::decom] & 0xF0));

        // BAC_AUTO is 0: anything else is Manual to T3000's grid
        // (BacnetInput.cpp:1509), and so here.
        if (from_file[input_at::auto_manual] != 0)
        {
            memcpy(&p[input_at::value], &from_file[input_at::value], 4);
            p[input_at::control] = from_file[input_at::control];
        }
        return p;
    }

    ImportedInputs imported_inputs(const ProgFile& file, int rows)
    {
        ImportedInputs out;
        for (int i = 0; i < (int)file.inputs.size(); i++)
        {
            const InputBytes kept = imported_input(i, file.inputs[(size_t)i]);
            if (kept == default_input(i))
                continue;
            if (i < rows)
                out.inputs.emplace_back(i, kept);
            else
                out.past++;
        }
        return out;
    }

    bool base64_decode(const std::string& text, std::vector<uint8_t>& out)
    {
        out.clear();
        if (text.size() % 4 != 0)
            return false;

        out.reserve(text.size() / 4 * 3);
        for (size_t at = 0; at < text.size(); at += 4)
        {
            const bool last = at + 4 == text.size();
            int digits[4];
            int padding = 0;
            for (int k = 0; k < 4; k++)
            {
                const char c = text[at + k];
                if (c == '=' && last && k >= 2)
                {
                    padding++;
                    digits[k] = 0;
                    continue;
                }
                // A digit after padding, or padding anywhere else.
                if (padding != 0)
                    return false;
                digits[k] = base64_digit(c);
                if (digits[k] < 0)
                    return false;
            }

            const uint32_t n = ((uint32_t)digits[0] << 18) | ((uint32_t)digits[1] << 12) |
                               ((uint32_t)digits[2] << 6) | (uint32_t)digits[3];
            out.push_back((uint8_t)(n >> 16));
            if (padding < 2)
                out.push_back((uint8_t)(n >> 8));
            if (padding < 1)
                out.push_back((uint8_t)n);
        }
        return true;
    }

    std::string base64_encode(const std::vector<uint8_t>& data)
    {
        static const char digits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        out.reserve((data.size() + 2) / 3 * 4);
        for (size_t at = 0; at < data.size(); at += 3)
        {
            const size_t left = data.size() - at;
            const uint32_t n = ((uint32_t)data[at] << 16) | (left > 1 ? (uint32_t)data[at + 1] << 8 : 0) |
                               (left > 2 ? (uint32_t)data[at + 2] : 0);
            out += digits[(n >> 18) & 63];
            out += digits[(n >> 12) & 63];
            out += left > 1 ? digits[(n >> 6) & 63] : '=';
            out += left > 2 ? digits[n & 63] : '=';
        }
        return out;
    }

    long uart_rate(uint8_t code)
    {
        static const long rates[] = { 1200, 2400, 3600, 4800, 7200, 9600, 19200, 38400, 57600, 115200, 921600 };
        return code < sizeof(rates) / sizeof(rates[0]) ? rates[code] : 0;
    }

    namespace
    {
        // Str_program_point's description (STR_PROGRAM_DESCRIPTION_LENGTH).
        constexpr size_t kProgramDescriptionLength = 21;

        // Writes "IN1", "OUT64" and the like into an item's description, as
        // T3000's sprintf does: the name, then its 0.
        void name_items(std::vector<uint8_t>& f, size_t table, const char* prefix)
        {
            const ProgSection& s = prog_sections()[table];
            for (int i = 0; i < s.items; i++)
            {
                char name[16];
                const int n = snprintf(name, sizeof name, "%s%d", prefix, i + 1);
                memcpy(&f[prog_table_at(table) + (size_t)i * s.size], name, (size_t)n);
            }
        }

        void put_le(std::vector<uint8_t>& f, size_t at, uint32_t value, int bytes)
        {
            for (int k = 0; k < bytes; k++)
                f[at + (size_t)k] = (uint8_t)(value >> (8 * k));
        }

        uint32_t le(const uint8_t* p, int bytes)
        {
            uint32_t v = 0;
            for (int k = 0; k < bytes; k++)
                v |= (uint32_t)p[k] << (8 * k);
            return v;
        }

        bool all_zero(const uint8_t* p, size_t n)
        {
            return std::all_of(p, p + n, [](uint8_t b) { return b == 0; });
        }

        // An item's description as T3000 shows it: up to its first 0.
        std::string item_name(const std::vector<uint8_t>& f, size_t table, int item, size_t length)
        {
            const uint8_t* p = &f[prog_table_at(table) + (size_t)item * prog_sections()[table].size];
            return std::string(reinterpret_cast<const char*>(p), strnlen(reinterpret_cast<const char*>(p), length));
        }

        std::string port_mode(uint8_t mode)
        {
            // NOUSE is the first of T3000's port modes (ud_str.h).
            return mode == 0 ? std::string("not used") : "in mode " + std::to_string(mode);
        }
    }

    std::vector<uint8_t> write_prog_file(const ProgExport& device)
    {
        const int version = kLastProgVersion;
        std::vector<uint8_t> f(prog_file_length(version), 0);
        f[0]                 = 0x55;
        f[1]                 = 0xFF;
        f[prog_at::version]  = (uint8_t)version;

        for (size_t i = 0; i < device.inputs.size() && i < (size_t)kProgInputs; i++)
            memcpy(&f[prog_at::inputs + i * wire::kInputPointWireSize], device.inputs[i].data(), wire::kInputPointWireSize);

        name_items(f, prog_table::outputs, "OUT");
        const ProgSection& outputs = prog_sections()[prog_table::outputs];
        for (int i = 0; i < outputs.items; i++)
            f[prog_table_at(prog_table::outputs) + (size_t)i * outputs.size +
              offsetof(wire::OutputPoint, hw_switch_status)] = 1;
        name_items(f, prog_table::variables, "VAR");
        name_items(f, prog_table::programs, "PRG");

        const ProgSection& flags = prog_sections()[prog_table::schedule_flags];
        memset(&f[prog_table_at(prog_table::schedule_flags)], 0xFF, (size_t)flags.items * flags.size);

        const size_t s = prog_table_at(prog_table::settings);
        f[s + wire::settings_at::mini_type] = device.mini_type;
        put_le(f, s + wire::settings_at::serial_number, device.serial, 4);
        f[s + wire::settings_at::com_baudrate0] = kUart115200;
        f[s + wire::settings_at::com_baudrate2] = kUart115200;
        f[s + wire::settings_at::ip_addr + 0]   = 192;
        f[s + wire::settings_at::ip_addr + 1]   = 168;
        f[s + wire::settings_at::ip_addr + 2]   = 0;
        f[s + wire::settings_at::ip_addr + 3]   = 3;
        put_le(f, s + wire::settings_at::modbus_port, 502, 2);
        return f;
    }

    std::string describe_prog_export(const std::vector<uint8_t>& f, const std::string& model)
    {
        if (f.size() != prog_file_length(kLastProgVersion))
            return std::string();

        const auto& sections = prog_sections();
        const auto table_zero = [&](size_t t) {
            return all_zero(&f[prog_table_at(t)], (size_t)sections[t].items * sections[t].size);
        };

        // The tables Load File puts over the panel's.
        const int outputs   = sections[prog_table::outputs].items;
        const int variables = sections[prog_table::variables].items;
        const int programs  = sections[prog_table::programs].items;
        bool auto_switches = true;
        for (int i = 0; i < outputs; i++)
            auto_switches = auto_switches &&
                            f[prog_table_at(prog_table::outputs) + (size_t)i * sections[prog_table::outputs].size +
                              offsetof(wire::OutputPoint, hw_switch_status)] == 1;

        struct Named { size_t table; const char* name; };
        static const Named others[] = {
            { 4, "PID loops" }, { 5, "screens" }, { 6, "graphic labels" }, { 7, "logins" },
            { 8, "custom units" }, { 9, "range tables" }, { 11, "schedules" }, { 12, "holidays" },
            { 13, "trend logs" }, { 14, "schedule times" }, { 15, "holiday codes" }, { 16, "program code" },
            { 17, "variable units" }, { 18, "multi-state values" },
        };
        std::vector<std::string> empty, held;
        for (const Named& n : others)
            (table_zero(n.table) ? empty : held).push_back(n.name);
        const auto join = [](const std::vector<std::string>& names) {
            std::string out;
            for (size_t i = 0; i < names.size(); i++)
                out += (i == 0 ? "" : i + 1 == names.size() ? " and " : ", ") + names[i];
            return out;
        };

        std::string text =
            "This file is for T3000's Load File, and holds every table of a panel, not only its inputs. Loaded "
            "onto a panel, it puts them all in place of the panel's: the inputs as configured here; the outputs "
            "as " + item_name(f, prog_table::outputs, 0, wire::kOutputDescriptionLength) + " to " +
            item_name(f, prog_table::outputs, outputs - 1, wire::kOutputDescriptionLength) +
            (auto_switches ? ", their hand switches at Auto" : "") + "; the variables as " +
            item_name(f, prog_table::variables, 0, wire::kVariableDescriptionLength) + " to " +
            item_name(f, prog_table::variables, variables - 1, wire::kVariableDescriptionLength) +
            "; the programs as " + item_name(f, prog_table::programs, 0, kProgramDescriptionLength) + " to " +
            item_name(f, prog_table::programs, programs - 1, kProgramDescriptionLength) +
            (table_zero(16) ? ", with no code" : "");
        if (!empty.empty())
            text += "; and no " + join(empty);
        if (!held.empty())
            text += "; and the " + join(held) + " the file holds";
        text += ".\n\n";

        // The settings Load File takes from the file.
        const uint8_t* s = &f[prog_table_at(prog_table::settings)];
        namespace at = wire::settings_at;
        const uint8_t modes[3] = { s[at::com0_config], s[at::com1_config], s[at::com2_config] };
        const uint8_t bauds[3] = { s[at::com_baudrate0], s[at::com_baudrate1], s[at::com_baudrate2] };
        std::string ports;
        if (modes[0] == modes[1] && modes[1] == modes[2])
            ports = "serial ports 0, 1 and 2 " + port_mode(modes[0]) + ", at " + std::to_string(uart_rate(bauds[0])) +
                    ", " + std::to_string(uart_rate(bauds[1])) + " and " + std::to_string(uart_rate(bauds[2])) + " baud";
        else
            for (int p = 0; p < 3; p++)
                ports += (p == 0 ? "" : p == 2 ? " and " : ", ") + std::string("serial port ") + std::to_string(p) + " " +
                         port_mode(modes[p]) + " at " + std::to_string(uart_rate(bauds[p])) + " baud";

        // Every byte the sentence names, or Load File keeps, is left out of
        // "every other setting".
        std::vector<uint8_t> rest(s, s + wire::kSettingsWireSize);
        const auto named = [&](size_t from, size_t n) { memset(&rest[from], 0, n); };
        named(at::ip_addr, 18);                  // kept: IP, subnet, gateway, MAC
        named(at::tcp_type, 2);                  // the IP address's mode, panel type
        named(at::com0_config, 3);
        named(at::com_baudrate0, 3);
        named(at::panel_type, 1);
        named(at::panel_name, at::panel_name_length);   // kept
        named(at::panel_number, 1);              // kept
        named(at::serial_number, 4);             // kept
        named(at::mstp_network, 2);
        named(at::modbus_port, 2);
        named(at::modbus_id, 1);                 // kept
        named(at::object_instance, 4);           // kept
        named(at::max_master, 1);
        named(at::reset_default, 1);             // Load File sets it to 0

        // T3000's Settings shows a tcp_type of 1 as Obtain IP Address
        // Automatically, and 0 or 2 as Use The Following IP Address, and
        // writes 0 for the second (BacnetSetting.cpp:361-371,
        // BacnetSettingTcpip.cpp:153-156, 204-207). ud_str.h's comment on
        // the field says the reverse; its captions are what the page names.
        const std::string ip =
            s[at::tcp_type] == 1
                ? std::string("the IP address set to Obtain IP Address Automatically, so a panel with an address set "
                              "by hand takes one from DHCP")
                : std::string("the IP address set to Use The Following IP Address, so a panel that obtains its "
                              "address automatically keeps the one it has now, fixed");
        text += "Of the settings, Load File keeps the panel's serial, name, panel number, Modbus id, object "
                "instance, IP address, subnet, gateway and MAC, and sets the others as the file has them: panel "
                "type " + std::to_string(s[at::mini_type]) + " (" + model + "), so load it only onto a " + model +
                "; " + ip + "; " + ports + "; Modbus TCP port " + std::to_string(le(s + at::modbus_port, 2)) +
                "; MS/TP network " + std::to_string(le(s + at::mstp_network, 2)) + " and max master " +
                std::to_string(s[at::max_master]) + "; its product field " + std::to_string(s[at::panel_type]) + "; and " +
                (all_zero(rest.data(), rest.size()) ? "every other setting 0" : "every other setting as the file has it") +
                ". The file says serial " + std::to_string(le(s + at::serial_number, 4)) + ".";
        return text;
    }
}
