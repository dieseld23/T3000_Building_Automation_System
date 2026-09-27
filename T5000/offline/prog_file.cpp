#include "prog_file.h"

#include <string.h>

#include <algorithm>

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
}
