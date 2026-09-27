#pragma once

// The ranges an input configured offline can be given, as T3000's Range
// dialog gives them (BacnetRange, opened from the Range column of the input
// grid): which rows it opens on, and which of its ranges each row can have.
//
// A range is named by the number the dialog shows in its box
// (IDC_EDIT_RANGE_SELECT), which is what its OK reads (BacnetRange.cpp:1158):
// 0 is Unused, 1 to 30 the digital range of that number, and a number above
// 30 the analog range 30 below it. So 41 is analog range 11, 0.0 to 5.0
// Volts, and 1 is digital range 1, Off/On.
//
// Like input_edit.h, nothing here can send anything to a device.

#include <stdint.h>

#include <string>
#include <vector>

#include "../device/product.h"

namespace t5000::offline
{
    // One of the dialog's ranges.
    struct InputRangeChoice
    {
        int number;            // as in the dialog's box

        // Its radio button's caption in T3000.rc, without the number T3000
        // puts before most of them, and with runs of spaces made one. The
        // digital ranges' are Digital_Units_Array's, and 0 is "Unused", what
        // the Range column shows for it.
        const char* caption;

        // "°C" for the first of a temperature sensor's pair, whose caption
        // does not say: the dialog's °C button picks it, and its °F button
        // the next range, whose caption says Deg.F (BacnetRange.cpp:1397).
        // Empty for every other range.
        const char* scale;
    };

    // Every range T5000 offers, in the dialog's order: Unused, the digital
    // ranges 1-22, then the analog ranges by number.
    //
    // Not offered, though the dialog has a button for each:
    //   - the custom digital ranges, 23-30, which are named from the panel
    //     and are blank until those names are read;
    //   - the analog buttons with no name, 65 and 67-69 (ranges 35, 37-39);
    //   - the multi-state ranges, 101-103, which T3000's input grid cannot
    //     show: it looks a digital input's range up in Digital_Units_Array,
    //     which has 23 entries (BacnetInput.cpp:1877).
    const std::vector<InputRangeChoice>& input_range_choices();

    // The range numbered `number`, or null when T5000 offers none by it.
    const InputRangeChoice* find_input_range(int number);

    // The range as the page names it: "Off/On", "0.0 to 5.0 Volts",
    // "10K Type2 °C".
    std::string input_range_name(const InputRangeChoice& choice);

    // The bytes a range sets. Only these two: T3000 leaves an input's value,
    // state, calibration and signal type as they were when its range changes
    // (BacnetInput.cpp:1778-1895).
    struct InputRangeBytes
    {
        uint8_t digital_analog;   // BAC_UNITS_ANALOG 1, BAC_UNITS_DIGITAL 0
        uint8_t range;
    };

    // 0 sets analog range 0, as T3000's OK does, so a range is never
    // digital range 0 once the dialog has been used (BacnetInput.cpp:1778).
    InputRangeBytes input_range_bytes(int number);

    // The number the dialog opens with for an input: its range, plus 30 when
    // analog and not 0 (BacnetRange.cpp:774-797).
    int input_range_number(uint8_t digital_analog, uint8_t range);

    // Whether the grid ignores a click on the Range cell of row `row`
    // (0-based): the rows some models hold at a fixed range
    // (BacnetInput.cpp:1657-1722).
    bool input_range_fixed(device::ProductClassId product, device::MiniType type, int row);

    // The numbers of the ranges the dialog lets row `row` have, in
    // input_range_choices' order. Empty when the row's range is fixed.
    //
    // The dialog changes which of its buttons can be pressed by the panel's
    // type and the row (BacnetRange.cpp:917-1026): the fast pulse count and
    // RPM only on the inputs that count fast pulses, and on some rows of the
    // T3-OEM only those or only a 10K Type2 sensor. It offers PT 1K only on a
    // panel whose settings say it supports it (special_flag), and nothing has
    // been read from a device configured offline, so it is never offered here.
    std::vector<int> input_ranges_offered(device::ProductClassId product, device::MiniType type, int row);
}
