#include "custom_ranges.h"

#include "device_text.h"

namespace t5000::display
{
    namespace
    {
        // STR_VARIABLE_DESCRIPTION_LENGTH (ud_str.h:174), which T3000 uses as
        // the limit on a table name's strlen (:4336).
        constexpr size_t kLongTableName = 21;

        // The byte T3000 puts in a table name's last position to mark the
        // table's precision (:4342, and ud_str.h:1182's comment).
        constexpr uint8_t kPrecisionMarker = 0xEF;

        // A name of this many UTF-16 units or more is dropped (:5578, :5584).
        constexpr size_t kLongStateName = 12;

        std::wstring state_name_wide(const uint8_t* unit, size_t field_offset)
        {
            // strlen from the field: through the on-name when the off-name
            // has no NUL, and to the end of the unit either way. Past the
            // unit, T3000's strlen reads the next unit of its own copy -
            // which, when a panel is first read, is still zero from
            // initialisation (global_function.cpp:17830-17834), so the name
            // ends here too.
            const std::wstring name = acp_to_wide(unit + field_offset, wire::kCustomUnitWireSize - field_offset);
            return name.size() >= kLongStateName ? std::wstring() : name;
        }
    }

    std::string digital_state_name(const uint8_t* unit, size_t field_offset)
    {
        return wide_to_utf8(state_name_wide(unit, field_offset));
    }

    std::string analog_table_name(const uint8_t* table)
    {
        // :4336-4352. A name with no NUL in its first 22 bytes is "too long":
        // T3000 zeroes its stored copy, which clears the last byte, so the
        // precision test below then fails and all nine bytes are used. The
        // strlen can run into the table's data, but a NUL anywhere in the
        // first 22 bytes settles it, and those are all inside the table.
        const bool too_long = length_to_nul(table, kLongTableName + 1) > kLongTableName;

        // Otherwise a 0xEF in the last byte is a precision marker, not text:
        // only the first eight bytes are the name.
        const size_t bytes = (!too_long && table[wire::analog_table_at::name_length - 1] == kPrecisionMarker)
                                 ? wire::analog_table_at::name_length - 1
                                 : wire::analog_table_at::name_length;

        return acp_to_utf8(table + wire::analog_table_at::name, bytes);
    }

    bool take_digital_ranges(const uint8_t* entities, size_t length, int first, int count,
                             CustomRanges& out)
    {
        if (entities == nullptr || first < 0 || count <= 0 || first + count > wire::kCustomUnitCount ||
            length != (size_t)count * wire::kCustomUnitWireSize)
            return false;

        for (int i = 0; i < count; i++)
        {
            const uint8_t* unit = entities + (size_t)i * wire::kCustomUnitWireSize;

            std::wstring off = state_name_wide(unit, wire::custom_unit_at::off);
            std::wstring on  = state_name_wide(unit, wire::custom_unit_at::on);
            if (unit[wire::custom_unit_at::direct] != kDigitalDirect)
                off.swap(on);

            // Custom_Digital_Range[i] = off + "/" + on (:5598), and the split
            // the Inputs grid makes of it to pick a state (BacnetInput.cpp:1162).
            const std::wstring text = off + L"/" + on;
            const std::vector<std::wstring> parts = split_like_t3000(text, L'/');

            DigitalRange& r = out.digital[first + i];
            r.text       = wide_to_utf8(text);
            r.has_states = parts.size() == 2;
            r.off        = r.has_states ? wide_to_utf8(parts[0]) : std::string();
            r.on         = r.has_states ? wide_to_utf8(parts[1]) : std::string();
        }

        // receive_custom_unit is set by the reply that ends at the last unit.
        if (first + count == wire::kCustomUnitCount)
            out.digital_known = true;
        return true;
    }

    bool take_analog_tables(const uint8_t* entities, size_t length, int first, int count,
                            CustomRanges& out)
    {
        if (entities == nullptr || first < 0 || count <= 0 || first + count > wire::kAnalogTableCount ||
            length != (size_t)count * wire::kAnalogTableWireSize)
            return false;

        for (int i = 0; i < count; i++)
        {
            out.analog[first + i]       = analog_table_name(entities + (size_t)i * wire::kAnalogTableWireSize);
            out.analog_known[first + i] = true;
        }
        return true;
    }
}
