#pragma once

// The names a panel stores for its custom ranges, cut out of the replies the
// way T3000 cuts them.
//
//   Digital ranges 23-30 take their state names from READUNIT_T3000: eight
//   off/on pairs (global_function.cpp:5544-5603).
//   Analog ranges 20-24 take their unit name from READANALOG_CUS_TABLE_T3000:
//   five tables, each with a name (:4316-4368).
//
// Both rules read the bytes in ways a plain "copy the field" would not - a
// name can run past its field, a precision marker hides in the last byte of
// another - so these work on the reply's bytes, not on a decoded struct.

#include <stddef.h>
#include <stdint.h>

#include <string>

#include "../wire/panel.h"

namespace t5000::display
{
    // DIGITAL_DIRECT (global_define.h:2483). A unit whose direct byte is this
    // keeps its names as sent; any other value swaps them
    // (global_function.cpp:5585).
    inline constexpr uint8_t kDigitalDirect = 0;

    struct DigitalRange
    {
        // Custom_Digital_Range[i]: off + "/" + on, after the direction swap.
        // This is what the Range column shows.
        std::string text;

        // The two state names the Value column chooses between, as T3000
        // splits `text` back apart. has_states is false when that split does
        // not give exactly two parts - a name containing "/", say - and T3000
        // then writes no value at all.
        bool        has_states = false;
        std::string off;
        std::string on;
    };

    struct CustomRanges
    {
        // Set once READUNIT_T3000 has answered for all eight - T3000's
        // receive_custom_unit (:5558-5562). Until then T3000's grid does not
        // use these names.
        bool         digital_known = false;
        DigitalRange digital[wire::kCustomUnitCount];

        // Per table, because T3000 reads tables 0-3 and then 4 in two
        // requests (BacnetView.cpp:6563-6565), and uses whatever arrived:
        // Analog_Custom_Units is not gated by a flag.
        bool        analog_known[wire::kAnalogTableCount] = {};
        std::string analog[wire::kAnalogTableCount];   // Analog_Custom_Units
    };

    // Fills `digital` from a READUNIT_T3000 reply's entities: `count` units
    // of 25 bytes, the first of which is unit `first`. Sets digital_known
    // when the reply ends at the last unit, as T3000 does. Returns false,
    // changing nothing, if the length is not count * 25 or the range runs
    // past unit 7.
    bool take_digital_ranges(const uint8_t* entities, size_t length, int first, int count,
                             CustomRanges& out);

    // Fills `analog` from a READANALOG_CUS_TABLE_T3000 reply: `count` tables
    // of 105 bytes, the first of which is table `first`.
    bool take_analog_tables(const uint8_t* entities, size_t length, int first, int count,
                            CustomRanges& out);

    // One off or on name, from a 25-byte unit. T3000's strlen starts at the
    // field and runs to the first NUL - through the rest of the unit if the
    // field has none - and a name of 12 or more UTF-16 units is dropped.
    std::string digital_state_name(const uint8_t* unit, size_t field_offset);

    // One table's name, from a 105-byte table.
    std::string analog_table_name(const uint8_t* table);
}
