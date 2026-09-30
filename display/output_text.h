#pragma once

// What T3000's Outputs grid shows for one point, column by column.
//
// A port of the loop in BacnetOutput.cpp:658-1141, as input_text.h is of the
// Inputs loop: the same text from the same struct. Where T3000 would show
// something T5000 cannot - a cell it leaves as it was, holding whatever the
// last refresh or the previous row put there - the text says what T5000
// knows, and `note` says why it differs.
//
// Three of T3000's columns are not ported yet: Panel, Type and Product Name.
// Type needs GetOutputType (global_function.cpp:17061), a per-model table of
// its own; Inputs does not show its Panel or Type either; and T5000's product
// names are not the ones GetProductName gives. An output on a sub-device -
// the row the Product columns are for - says so in its note, and its Product
// Output text is here.

#include <string>

#include "custom_ranges.h"
#include "../device/output_rows.h"
#include "../wire/points.h"

namespace t5000::display
{
    // What T3000 knows about the panel when it fills the grid.
    struct OutputPanel
    {
        // The settings were read. When not, T5000 applies no model's rules:
        // no HOA switches, and no output is shown as external.
        bool known = false;

        // bacnet_device_type: the settings' mini_type.
        int type = 0;

        // What the chain decided for that type.
        device::OutputRows rows;

        // The names the panel stores for its custom digital ranges, where
        // they were read.
        CustomRanges ranges;
    };

    struct OutputText
    {
        std::string full_label;   // trimmed, as T3000 trims it
        std::string label;        // trimmed
        std::string auto_manual;  // "Auto" / "Manual"; "" when the switch overrides it
        std::string hoa;          // "AUTO" / "MAN-OFF" / "MAN-ON"; "" on panels without switches
        bool        hand = false; // MAN-OFF or MAN-ON: T3000 shows the row in red
        std::string value;        // "7.35", or a state name such as "On"
        std::string units;        // "Volts"; empty for digital points
        std::string range;        // "0.0 -> 10", "Off/On", "Out of range"
        std::string low_voltage;  // "2.5"; empty for 0
        std::string high_voltage;
        std::string pwm_period;   // "%u"
        std::string status;       // "OK" / "-" / ""

        // An output on a sub-device, which T3000 marks External and names
        // the product of. product_output is its Product Output column:
        // "DO3", "AO1".
        bool        external = false;
        std::string product_output;

        // Empty when this is exactly what T3000 shows. Otherwise, why not.
        std::string note;
    };

    // index is the point's position, 0-based: the switches cover the first
    // outputs of a model.
    OutputText output_text(const wire::OutputPoint& p, int index, const OutputPanel& panel);
}
