#include "input_edit.h"

#include <stdio.h>
#include <string.h>

#include <algorithm>

#include <windows.h>

#include "input_ranges.h"

namespace t5000::offline
{
    namespace
    {
        constexpr size_t kDescriptionLength = wire::kDescriptionLength;   // STR_IN_DESCRIPTION_LENGTH
        constexpr size_t kLabelLength       = wire::kLabelLength;         // STR_IN_LABEL

        std::string field_text(const uint8_t* field, size_t length)
        {
            return std::string((const char*)field, strnlen((const char*)field, length));
        }

        bool utf8_to_wide(const std::string& utf8, std::wstring& wide)
        {
            wide.clear();
            if (utf8.empty())
                return true;

            const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), (int)utf8.size(), nullptr, 0);
            if (n <= 0)
                return false;
            wide.assign((size_t)n, L'\0');
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), (int)utf8.size(), &wide[0], n);
            return true;
        }

        // WideCharToMultiByte(CP_ACP, 0, ...), as T3000 calls it
        // (BacnetInput.cpp:518, :565), refused when it loses anything.
        //
        // T3000 passes no flags, so a character the code page lacks becomes
        // "?", or a look-alike where Windows has a best fit ("é" to "e" on
        // some code pages). Neither is what was typed, and nothing says so.
        // Converting back and comparing catches both.
        bool wide_to_acp(const std::wstring& wide, unsigned code_page, std::string& acp, std::string& message)
        {
            acp.clear();
            if (wide.empty())
                return true;

            const int n = WideCharToMultiByte(code_page, 0, wide.data(), (int)wide.size(), nullptr, 0, nullptr, nullptr);
            if (n > 0)
            {
                acp.assign((size_t)n, '\0');
                WideCharToMultiByte(code_page, 0, wide.data(), (int)wide.size(), &acp[0], n, nullptr, nullptr);

                const int back = MultiByteToWideChar(code_page, 0, acp.data(), (int)acp.size(), nullptr, 0);
                std::wstring again((size_t)(back > 0 ? back : 0), L'\0');
                if (back > 0)
                    MultiByteToWideChar(code_page, 0, acp.data(), (int)acp.size(), &again[0], back);
                if (again == wide)
                    return true;
            }

            acp.clear();
            message = "It holds a character that this computer's code page (" +
                      std::to_string(code_page == CP_ACP ? GetACP() : code_page) +
                      ") cannot store, and T3000 would store a different one in its place.";
            return false;
        }

        // CString::MakeUpper as T3000 calls it (BacnetInput.cpp:496). T3000
        // never calls setlocale, so the C runtime is in the "C" locale, where
        // _wcsupr changes a to z and nothing else.
        void upper_like_t3000(std::wstring& s)
        {
            for (auto& c : s)
            {
                if (c >= L'a' && c <= L'z')
                    c = (wchar_t)(c - L'a' + L'A');
            }
        }

        // The names T3000 gives points of other kinds on a panel it has not
        // read, which a full label may not repeat (Initial_All_Point,
        // global_function.cpp:17727-17757; Check_FullLabel_Exsit, :3151-3235).
        //
        // The configuration kept offline holds a panel's inputs only, so its
        // outputs, variables, PVARs and programs are as T3000 starts them.
        // Their labels, and every name of its screens, schedules and holidays,
        // are empty, and an empty name matches nothing: an empty new one is
        // never checked (:3155-3156).
        bool is_default_name_of_another_kind(const std::string& acp)
        {
            struct Kind
            {
                const char* prefix;
                int         count;
            };
            static const Kind kKinds[] = {
                { "OUT", t3000_counts::kOutputs },
                { "VAR", t3000_counts::kVariables },
                { "PVAR", t3000_counts::kPvars },
                { "PRG", t3000_counts::kPrograms },
            };

            for (const Kind& k : kKinds)
            {
                for (int i = 1; i <= k.count; i++)
                {
                    char name[16];
                    snprintf(name, sizeof(name), "%s%d", k.prefix, i);
                    if (acp == name)
                        return true;
                }
            }
            return false;
        }

        // A grid cell holds a line of text. A control character - a NUL
        // above all, which T3000's conversion would stop at, keeping only
        // what came before it - is refused rather than stored.
        bool has_control_character(const std::wstring& s)
        {
            for (const wchar_t c : s)
            {
                if (c < 0x20 || c == 0x7F)
                    return true;
            }
            return false;
        }

        std::string quoted(const std::string& utf8)
        {
            return "\"" + utf8 + "\"";
        }

        void set_text(InputBytes& p, size_t at, size_t length, const std::string& acp)
        {
            // memcpy_s of `length` bytes from a zeroed buffer (:517-519,
            // :564-566): the text, then zeros to the end of the field.
            memset(&p[at], 0, length);
            memcpy(&p[at], acp.data(), acp.size());
        }

        bool edit_label(std::vector<InputBytes>& inputs, int index, const std::string& text,
                        unsigned code_page, InputBytes& p, std::string& message)
        {
            std::wstring wide;
            if (!utf8_to_wide(text, wide))
            {
                message = "The label is not valid text.";
                return false;
            }
            if (has_control_character(wide))
            {
                message = "The label holds a control character.";
                return false;
            }

            // T3000 counts UTF-16 code units, CString::GetLength (:476).
            if (wide.size() >= kLabelLength)
            {
                message = "A label is at most " + std::to_string(kLabelLength - 1) + " characters.";
                return false;
            }

            // :495-496, in this order.
            for (auto& c : wide)
            {
                if (c == L'-')
                    c = L'_';
            }
            upper_like_t3000(wide);

            std::string acp;
            if (!wide_to_acp(wide, code_page, acp, message))
            {
                message = "The label cannot be stored. " + message;
                return false;
            }

            // T3000 checks characters, then copies STR_IN_LABEL bytes, so a
            // label of fewer characters than that but as many bytes loses its
            // terminator, and a read gives it back empty (decode_input_point).
            // Refused here instead.
            if (acp.size() >= kLabelLength)
            {
                message = "In this computer's code page the label takes " + std::to_string(acp.size()) +
                          " bytes, and a label has room for " + std::to_string(kLabelLength - 1) + ".";
                return false;
            }

            // Already this input's label: nothing to change, as T3000 writes
            // nothing when the input is unchanged. T3000 would say it already
            // exists, since it checks the input itself too.
            wire::InputPoint mine;
            wire::decode_input_point(inputs[(size_t)index].data(), wire::kInputPointWireSize, mine);
            if (!acp.empty() && acp != field_text(mine.label, kLabelLength))
            {
                // Check_Label_Exsit (global_function.cpp:3242-3330): every
                // input's label, as T3000 holds it after reading the panel -
                // with "-" and "." folded to "_" (decode_input_point). Every
                // other kind's label is empty on a panel configured offline.
                for (size_t i = 0; i < inputs.size(); i++)
                {
                    wire::InputPoint other;
                    wire::decode_input_point(inputs[i].data(), wire::kInputPointWireSize, other);
                    if (acp == field_text(other.label, kLabelLength))
                    {
                        message = quoted(text) + " is already the label of input " + std::to_string(i + 1) +
                                  ". T3000 does not let two points share a label.";
                        return false;
                    }
                }
            }

            set_text(p, input_at::label, kLabelLength, acp);
            return true;
        }

        bool edit_full_label(std::vector<InputBytes>& inputs, int index, const std::string& text,
                             unsigned code_page, InputBytes& p, std::string& message)
        {
            std::wstring wide;
            if (!utf8_to_wide(text, wide))
            {
                message = "The full label is not valid text.";
                return false;
            }
            if (has_control_character(wide))
            {
                message = "The full label holds a control character.";
                return false;
            }

            if (wide.size() >= kDescriptionLength)
            {
                message = "A full label is at most " + std::to_string(kDescriptionLength - 1) + " characters.";
                return false;
            }

            std::string acp;
            if (!wide_to_acp(wide, code_page, acp, message))
            {
                message = "The full label cannot be stored. " + message;
                return false;
            }
            if (acp.size() >= kDescriptionLength)
            {
                message = "In this computer's code page the full label takes " + std::to_string(acp.size()) +
                          " bytes, and a full label has room for " + std::to_string(kDescriptionLength - 1) + ".";
                return false;
            }

            wire::InputPoint mine;
            wire::decode_input_point(inputs[(size_t)index].data(), wire::kInputPointWireSize, mine);
            if (!acp.empty() && acp != field_text(mine.description, kDescriptionLength))
            {
                // Check_FullLabel_Exsit (:3151-3235), case-sensitive: the
                // first BAC_INPUT_ITEM_COUNT inputs' full labels, then the
                // other kinds' names.
                for (size_t i = 0; i < inputs.size() && i < (size_t)t3000_counts::kInputs; i++)
                {
                    wire::InputPoint other;
                    wire::decode_input_point(inputs[i].data(), wire::kInputPointWireSize, other);
                    if (acp == field_text(other.description, kDescriptionLength))
                    {
                        message = quoted(text) + " is already the full label of input " + std::to_string(i + 1) +
                                  ". T3000 does not let two points share a full label.";
                        return false;
                    }
                }
                if (is_default_name_of_another_kind(acp))
                {
                    message = quoted(text) + " is the name T3000 gives one of this panel's other points. "
                              "T3000 does not let two points share a full label.";
                    return false;
                }
            }

            set_text(p, input_at::description, kDescriptionLength, acp);
            return true;
        }

        bool equals_ignoring_case(const std::string& a, const char* b)
        {
            return _stricmp(a.c_str(), b) == 0;
        }

        bool edit_auto_manual(const std::string& text, InputBytes& p, std::string& message)
        {
            // BAC_AUTO 0, BAC_MANUAL 1. The page sends the state the operator
            // chose, which is where T3000's click would take it: Manual from 0,
            // Auto from anything else (:1615-1654).
            if (equals_ignoring_case(text, "Auto"))
                p[input_at::auto_manual] = 0;
            else if (equals_ignoring_case(text, "Manual"))
                p[input_at::auto_manual] = 1;
            else
            {
                message = "Auto/Manual must be \"Auto\" or \"Manual\".";
                return false;
            }
            return true;
        }

        // A whole number from 0 to `most`, with spaces around it allowed and
        // nothing else: no sign, no fraction, no letters after it.
        bool whole_number(const std::string& text, int most, int& value)
        {
            const size_t first = text.find_first_not_of(" \t");
            const size_t last  = text.find_last_not_of(" \t");
            if (first == std::string::npos)
                return false;

            value = 0;
            for (size_t i = first; i <= last; i++)
            {
                const char c = text[i];
                if (c < '0' || c > '9')
                    return false;
                value = value * 10 + (c - '0');
                if (value > most)
                    return false;
            }
            return true;
        }

        bool edit_filter(const std::string& text, InputBytes& p, std::string& message)
        {
            // T3000 reads the cell with _wtoi and takes 0-255 (:662-673).
            // _wtoi reads "12abc" as 12 and "abc" as 0; here only a whole
            // number is taken.
            int value = 0;
            if (!whole_number(text, 255, value))
            {
                message = "The filter must be a whole number from 0 to 255.";
                return false;
            }

            p[input_at::filter] = (uint8_t)value;
            return true;
        }

        std::string model_name(const InputPanel& panel)
        {
            return device::panel_name(panel.product, panel.type);
        }

        bool edit_range(const InputPanel& panel, int index, const std::string& text, InputBytes& p,
                        std::string& message)
        {
            // The number the Range dialog's OK reads (BacnetRange.cpp:1158).
            // T3000 takes any number typed in its box, whether or not a
            // button offers it, and stores the range 30 below one above 30
            // in a byte; here only a range the dialog offers the row is
            // taken.
            int number = 0;
            const InputRangeChoice* choice = nullptr;
            if (whole_number(text, 999, number))
                choice = find_input_range(number);
            if (!choice)
            {
                message = "A range is chosen by its number in T3000's Range dialog: 0 for Unused, 1-22 for a "
                          "digital range, 31-66 for an analog one.";
                return false;
            }

            const std::vector<int> offered = input_ranges_offered(panel.product, panel.type, index);
            if (std::find(offered.begin(), offered.end(), number) == offered.end())
            {
                const std::string name = "\"" + input_range_name(*choice) + "\"";
                if (number == 39 || number == 40)
                {
                    message = "T3000 offers " + name + " only on a panel whose settings say it has a PT 1K "
                              "input, and nothing has been read from this one.";
                }
                else
                {
                    message = "T3000's Range dialog does not offer " + name + " for input " +
                              std::to_string(index + 1) + " of a " + model_name(panel) + ".";
                }
                return false;
            }

            const InputRangeBytes b = input_range_bytes(number);
            p[input_at::digital_analog] = b.digital_analog;
            p[input_at::range]          = b.range;
            return true;
        }
    }

    InputBytes default_input(int index)
    {
        // Str_in_point temp_in = {0}; filter = 5; sprintf(description,
        // "IN%d", i + 1) (global_function.cpp:17718-17722).
        InputBytes p = {};
        char name[kDescriptionLength] = {};
        snprintf(name, sizeof(name), "IN%d", index + 1);
        memcpy(&p[input_at::description], name, strnlen(name, sizeof(name)));
        p[input_at::filter] = 5;
        return p;
    }

    const char* input_field_name(InputField field)
    {
        switch (field)
        {
        case InputField::FullLabel:  return "fullLabel";
        case InputField::Label:      return "label";
        case InputField::AutoManual: return "autoManual";
        case InputField::Range:      return "range";
        case InputField::Filter:     return "filter";
        }
        return "";
    }

    bool input_field_from_name(const std::string& name, InputField& field)
    {
        for (const InputField f : input_fields())
        {
            if (name == input_field_name(f))
            {
                field = f;
                return true;
            }
        }
        return false;
    }

    std::vector<InputField> input_fields()
    {
        return { InputField::FullLabel, InputField::AutoManual, InputField::Range, InputField::Filter,
                 InputField::Label };
    }

    bool input_field_enabled(const InputPanel& panel, int index, const InputBytes& input, InputField field,
                             std::string* why)
    {
        std::string reason;
        switch (field)
        {
        case InputField::FullLabel:
        case InputField::Label:
        case InputField::AutoManual:
            return true;

        case InputField::Range:
            if (!input_range_fixed(panel.product, panel.type, index))
                return true;
            reason = "The range of input " + std::to_string(index + 1) + " of a " + model_name(panel) +
                     " is fixed: T3000 does not let it be changed.";
            break;

        case InputField::Filter:
            // BAC_UNITS_ANALOG. An input that is neither analog nor digital
            // is left by T3000 as the grid's last row left the cell; here
            // its filter cannot be changed.
            if (input[input_at::digital_analog] == 1)
                return true;
            reason = "Input " + std::to_string(index + 1) +
                     " is digital, and T3000 lets only an analog input's filter be changed. Give it an analog "
                     "range first.";
            break;
        }

        if (why)
            *why = reason;
        return false;
    }

    std::vector<InputField> editable_input_fields(const InputPanel& panel, int index, const InputBytes& input)
    {
        std::vector<InputField> out;
        for (const InputField f : input_fields())
            if (input_field_enabled(panel, index, input, f))
                out.push_back(f);
        return out;
    }

    bool apply_input_edit(std::vector<InputBytes>& inputs, const InputPanel& panel, int index, InputField field,
                          const std::string& text, bool& changed, std::string& message, unsigned code_page)
    {
        changed = false;

        if (index < 0 || (size_t)index >= inputs.size())
        {
            message = "This panel has no input " + std::to_string(index + 1) + ".";
            return false;
        }

        // Fresh_Input_Item does nothing past INPUT_LIMITE_ITEM_COUNT (:458):
        // those rows are shown empty and cannot be changed.
        if (index >= panel.rows)
        {
            message = "T3000 shows " + std::to_string(panel.rows) + " inputs for this model, so input " +
                      std::to_string(index + 1) + " cannot be changed.";
            return false;
        }

        InputBytes p = inputs[(size_t)index];
        if (!input_field_enabled(panel, index, p, field, &message))
            return false;

        bool ok = false;
        switch (field)
        {
        case InputField::FullLabel:  ok = edit_full_label(inputs, index, text, code_page, p, message); break;
        case InputField::Label:      ok = edit_label(inputs, index, text, code_page, p, message); break;
        case InputField::AutoManual: ok = edit_auto_manual(text, p, message); break;
        case InputField::Range:      ok = edit_range(panel, index, text, p, message); break;
        case InputField::Filter:     ok = edit_filter(text, p, message); break;
        }
        if (!ok)
            return false;

        // memcmp, as T3000 decides whether to write (:694).
        changed = p != inputs[(size_t)index];
        inputs[(size_t)index] = p;
        return true;
    }

    std::vector<std::string> changed_fields(const InputBytes& base, const InputBytes& edited)
    {
        auto differs = [&](size_t at, size_t length) {
            return memcmp(&base[at], &edited[at], length) != 0;
        };
        auto nibble_differs = [&](size_t at, uint8_t mask) {
            return (base[at] & mask) != (edited[at] & mask);
        };

        // The grid's columns, left to right, and the bytes each shows. Value
        // is the value for an analog input and the control byte for a digital
        // one (:1540-1550); Range and the analog/digital byte go together.
        std::vector<std::string> out;
        if (differs(input_at::description, kDescriptionLength))
            out.push_back("fullLabel");
        if (differs(input_at::value, 4) || differs(input_at::control, 1))
            out.push_back("value");
        if (differs(input_at::auto_manual, 1))
            out.push_back("autoManual");
        if (nibble_differs(input_at::decom, 0x0F))
            out.push_back("status");
        if (differs(input_at::range, 1) || differs(input_at::digital_analog, 1))
            out.push_back("range");
        if (differs(input_at::calibration_h, 2) || differs(input_at::calibration_sign, 1))
            out.push_back("calibration");
        if (differs(input_at::filter, 1))
            out.push_back("filter");
        if (nibble_differs(input_at::decom, 0xF0))
            out.push_back("signalType");
        if (differs(input_at::label, kLabelLength))
            out.push_back("label");
        if (differs(input_at::sub_id, 2) || differs(input_at::sub_number, 1))
            out.push_back("external");
        return out;
    }

    bool utf8_to_code_page(const std::string& utf8, unsigned code_page, std::string& out, std::string& message)
    {
        std::wstring wide;
        if (!utf8_to_wide(utf8, wide))
        {
            out.clear();
            message = "It is not valid text.";
            return false;
        }
        return wide_to_acp(wide, code_page, out, message);
    }
}
