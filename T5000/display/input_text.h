#pragma once

// What T3000's Inputs grid shows for one point, column by column.
//
// A port of the loop in BacnetInput.cpp:951-1237, producing the same text
// from the same struct. Where T3000 would show something T5000 cannot - a
// custom range whose names the panel did not send, or a cell T3000 leaves
// holding the previous row's text - the text says what T5000 knows, and
// `note` says why it differs.

#include <string>

#include "custom_ranges.h"
#include "../device/product.h"
#include "../wire/points.h"

namespace t5000::display
{
    // What T3000 knows about the panel when it fills the grid.
    struct PanelContext
    {
        // What T3000 calls bacnet_device_type: the mini_type from the panel's
        // settings (global_function.cpp:5270). Not known when the settings
        // were not read - and the one per-model branch below (RMC1232) is
        // then not taken, rather than guessed.
        bool             known = false;
        device::MiniType type  = device::MiniType::NotSet;

        // The names the panel stores for its custom ranges, where they were
        // read.
        CustomRanges ranges;
    };

    struct InputText
    {
        std::string auto_manual;  // "Auto" / "Manual"
        std::string value;        // "21.50", or a state name such as "On"
        std::string units;        // "°C"; empty for digital points
        std::string range;        // "10K Type2", "Off/On", "Out of range"
        std::string calibration;  // "1.5"; empty for digital points
        std::string sign;         // "+" / "-"; empty for digital points
        std::string filter;       // "%d"
        std::string status;       // "Normal" / "Open" / "Shorted" / ""
        bool        alarm = false;  // Open or Shorted - T3000 shows these in red
        std::string signal_type;  // "Thermistor Dry Contact", "4-20 ma", ...

        // Empty when this is exactly what T3000 shows. Otherwise, why not.
        std::string note;
    };

    // index is the point's position, 0-based - T3000 labels a few inputs by
    // position on some models.
    InputText input_text(const wire::InputPoint& p, int index, const PanelContext& panel);

    // T3000's bac_Invalid_range (global_function.cpp:16699): above 30 and not
    // one of the multi-state ranges 101-104.
    bool is_invalid_range(uint8_t range);

    // value / 1000 with two decimals, computed in float as T3000 computes it.
    // In double some values round the other way.
    std::string thousandths(int32_t value);
}
