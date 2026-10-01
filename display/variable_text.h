#pragma once

// What T3000's Variables grid shows for one point, column by column.
//
// A port of the loop in Fresh_Variable_List (BacnetVariable.cpp:209-478), as
// output_text.h is of the Outputs loop. Where T3000 leaves a cell as it was -
// holding what the last refresh put there, which is nothing when the grid is
// first shown - the text says what T5000 knows, and `note` says why it
// differs. An empty note means the row is exactly what T3000 shows.
//
// T3000 has a third way of showing a variable, for a third-party BACnet
// device (bacnet_device_type 254, :446-454). A panel's settings give at most
// 63 for that type, so it is never taken for a panel T5000 reads.

#include <stdint.h>

#include <string>

#include "custom_ranges.h"
#include "variable_ranges.h"
#include "../wire/points.h"

namespace t5000::display
{
    // BAC_UNITS_DIGITAL (global_define.h:377). A variable whose
    // digital_analog is this is digital; any other value is analog - T3000
    // tests for this one value only (:305).
    inline constexpr uint8_t kVariableDigital = 0;

    // The names the grid can use, where they were read.
    struct VariablePanel
    {
        CustomRanges   ranges;   // digital ranges 23-30
        VariableRanges names;    // analog ranges 34-38, and 101-104
    };

    struct VariableText
    {
        std::string full_label;   // trimmed, as T3000 trims it
        std::string label;        // trimmed
        std::string auto_manual;  // "Auto" / "Manual"
        std::string value;        // "21.500", "On", "01:30:00", or a state's name
        std::string units;        // "°C", "Off/On", "Time", ...

        // Empty when this is exactly what T3000 shows. Otherwise, why not.
        std::string note;
    };

    VariableText variable_text(const wire::VariablePoint& p, const VariablePanel& panel);

    // value / 1000 with three decimals, computed in double as T3000 computes
    // it (:311-313) - three, where inputs and outputs show two. In float
    // until 2026-09-28, where 123456700 was 123456.703.
    std::string variable_number(int32_t value);

    // intervaltotextfull(textbuf, seconds, 0, 0) (global_function.cpp:7856):
    // "HH:MM:SS", with a leading "-" for a negative time and as many digits
    // of hours as it takes.
    std::string interval_text(long seconds);
}
