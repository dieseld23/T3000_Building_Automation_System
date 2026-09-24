#pragma once

// The text T3000 shows for a point's range, units and status, copied from
// T3000/global_define.h.
//
// Copied, not included: that header declares these as CString arrays and
// cannot compile outside MFC. The copy is checked rather than trusted, the way
// wire/points.h is - tables_guard_selftest.cpp parses the real header on every
// build and fails it if an entry, or the number of entries, differs. Treat that
// test as part of this file.
//
// The COUNT of each table is behaviour, not just size. T3000 picks between
// branches with sizeof(table)/sizeof(table[0]) - an analog input on range 37
// shows units only because Input_List_Analog_Units has more than 37 entries -
// so every bound in input_text.cpp is taken from count() below and never
// written as a literal. Three entries of Input_List_Analog_Units are commented
// out in the source; a copy that kept them would shift every unit after
// index 18 and still look plausible.
//
// UTF-8, spelled as bytes. The source writes the degree sign as °; a
// narrow literal with \u in it would be encoded in the compiler's execution
// character set, which is the system code page unless told otherwise.

#include <stddef.h>

namespace t5000::display
{
    template <size_t N>
    constexpr size_t count(const char* const (&)[N])
    {
        return N;
    }

    // Digital_Units_Array (global_define.h:823). A digital point's range
    // names its two states, "off/on" in that order: control 0 shows the
    // first, anything else the second.
    inline constexpr const char* const kDigitalUnits[] = {
        "Unused",          // 0
        "Off/On",          // 1
        "Close/Open",      // 2
        "Stop/Start",      // 3
        "Disable/Enable",  // 4
        "Normal/Alarm",    // 5
        "Normal/High",     // 6
        "Normal/Low",      // 7
        "No/Yes",          // 8
        "Cool/Heat",       // 9
        "Unoccupy/Occupy", // 10
        "Low/High",        // 11
        "On/Off",          // 12
        "Open/Close",      // 13
        "Start/Stop",      // 14
        "Enable/Disable",  // 15
        "Alarm/Normal",    // 16
        "High/Normal",     // 17
        "Low/Normal",      // 18
        "Yes/No",          // 19
        "Heat/Cool",       // 20
        "Occupy/Unoccupy", // 21
        "High/Low",        // 22
    };

    // Input_List_Analog_Units (global_define.h:894). The Units column of an
    // analog input, by range.
    inline constexpr const char* const kInputAnalogUnits[] = {
        "",                // 0
        "\xC2\xB0" "C",    // 1
        "\xC2\xB0" "F",    // 2
        "\xC2\xB0" "C",    // 3
        "\xC2\xB0" "F",    // 4
        "\xC2\xB0" "C",    // 5
        "\xC2\xB0" "F",    // 6
        "\xC2\xB0" "C",    // 7
        "\xC2\xB0" "F",    // 8
        "\xC2\xB0" "C",    // 9
        "\xC2\xB0" "F",    // 10
        "Volts",           // 11
        "Amps",            // 12
        "ma",              // 13
        "psi",             // 14
        "counts",          // 15
        "%",               // 16
        "%",               // 17
        "%",               // 18
        "Volts",           // 19
        "",                // 20  custom table 1 - see input_text.cpp
        "",                // 21  custom table 2
        "",                // 22  custom table 3
        "",                // 23  custom table 4
        "",                // 24  custom table 5
        "counts",          // 25
        "Hz",              // 26
        "%",               // 27
        "PPM",             // 28
        "RPM",             // 29
        "PPB",             // 30
        "ug/m3",           // 31
        "#/cm3",           // 32
        "dB",              // 33
        "Lux",             // 34
        "",                // 35
        "A",               // 36
        "",                // 37
        "",                // 38
        "",                // 39
    };

    // Input_Analog_Units_Array (global_define.h:941). The Range column of an
    // analog input. "Humidty" is T3000's spelling, and is what it shows.
    inline constexpr const char* const kInputAnalogRanges[] = {
        "Unused",                   // 0
        "Y3K -40 to 150",           // 1
        "Y3K -40 to 300",           // 2
        "10K Type2",                // 3
        "10K Type2",                // 4
        "G3K -40 to 120",           // 5
        "G3K -40 to 250",           // 6
        "10K Type3",                // 7
        "10K Type3",                // 8
        "PT 1K -200 to 300",        // 9
        "PT 1K -200 to 570",        // 10
        "0.0 to 5.0",               // 11
        "0.0 to 100",               // 12
        "4 to 20",                  // 13
        "4 to 20",                  // 14
        "Pulse Count (Slow 1Hz)",   // 15
        "0 to 100",                 // 16
        "0 to 100",                 // 17
        "0 to 100",                 // 18
        "0.0 to 10.0",              // 19
        "Table 1",                  // 20
        "Table 2",                  // 21
        "Table 3",                  // 22
        "Table 4",                  // 23
        "Table 5",                  // 24
        "Pulse Count (Fast 100Hz)", // 25
        "Frequency",                // 26
        "Humidty %",                // 27
        "CO2  PPM",                 // 28
        "Revolutions Per Minute",   // 29
        "TVOC PPB",                 // 30
        "ug/m3",                    // 31
        "#/cm3",                    // 32
        "dB",                       // 33
        "Lux",                      // 34
        "",                         // 35
        "-200A to 200A",            // 36
        "",                         // 37
        "",                         // 38
        "",                         // 39
    };

    // JumperStatus (global_define.h:1704). The Signal Type column: the
    // high nibble of an input's decom byte.
    inline constexpr const char* const kJumperStatus[] = {
        "Thermistor Dry Contact", // 0
        "4-20 ma",                // 1
        "0-5 V",                  // 2
        "0-10 V",                 // 3
        "Thermistor Dry Contact", // 4
        "PT 1K",                  // 5
    };

    // Decom_Array (global_define.h:1909). The Status column: the low nibble
    // of an input's decom byte. Not "decommissioned" - an input's 1 is an
    // open circuit.
    inline constexpr const char* const kInputStatus[] = {
        "Normal",  // 0
        "Open",    // 1
        "Shorted", // 2
    };
}
