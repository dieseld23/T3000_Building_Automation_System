#pragma once

// How T3000's input grid turns what is typed or chosen in the Value,
// Calibration and Signal Type cells into an input's bytes (Fresh_Input_Item,
// BacnetInput.cpp:597-693), and which states a digital input's Value
// switches between (OnNMClickList1, :1507-1552).
//
// Only arithmetic and tables: input_edit.h decides which cells an input lets
// be changed, and applies these. Like it, nothing here can send anything to
// a device. T5000Conformance builds this file too, to hold it to T3000's
// source.

#include <stdint.h>

#include <string>
#include <vector>

namespace t5000::offline
{
    // A number as typed in a cell: spaces or tabs around it, a sign, digits
    // with or without a fraction, and an exponent, as "12", "-2.5", ".5" or
    // "1e3". Nothing else: T3000 reads the cell with _wtof, which stops at
    // the first character it cannot use ("12abc" is 12, "abc" 0, "12,5" 12)
    // and reads "inf", "nan" and hex too; here those are refused rather than
    // read as some other number. Read in any locale as the C locale reads
    // it, as _wtof does in T3000, which never sets one.
    bool typed_number(const std::string& text, double& value);

    // An input's value from the number typed in its Value cell:
    // (int)(_wtof(text) * 1000), truncated towards zero, in thousandths
    // (:601). False when that is outside a 32-bit int: T3000's conversion
    // is undefined there, and on x86 gives INT_MIN.
    bool value_thousandths(double typed, int32_t& value);

    // The bytes a calibration typed in its cell sets (:631-661): the number
    // as a float, times 10 as a float, truncated, then its size in two bytes,
    // high and low. T3000 builds for x86 with SSE2 and precise floating point
    // (neither is set in T3000_VS2019.vcxproj, so both are the compiler's
    // default), so the product is rounded to a float before it is truncated:
    // 0.7 is 7 tenths, and 6553.59999 is 65536, which is refused.
    //
    // The sign is minus for a number below zero and plus for any other. That
    // is where T5000 differs: T3000 sets minus for a number below zero and
    // leaves the sign as it was for any other, so typing 2 after -2.5 keeps
    // it minus. The owner decided on 2026-09-26 that the sign follows what is
    // typed.
    //
    // False when the size is above 65535 tenths, which T3000 refuses too
    // ("Please Input an value between 0.0 - 6553.6").
    struct CalibrationBytes
    {
        uint8_t sign = 0;   // calibration_sign: 0 plus, 1 minus
        uint8_t high = 0;   // calibration_h
        uint8_t low  = 0;   // calibration_l
    };
    bool calibration_bytes(double typed, CalibrationBytes& bytes);

    // The names the grid's Signal Type list offers, in its order:
    // JumperStatus without its index 4, which repeats index 0's name
    // (BacnetInput.cpp:416-425).
    std::vector<std::string> signal_type_choices();

    // The signal type T3000 stores for a name chosen from that list, or -1
    // for a name that is not one of JumperStatus's. T3000 compares the name
    // with each of JumperStatus's without regard to case and, with no break,
    // stores the last that matches (:678-691): so "Thermistor Dry Contact",
    // which is both index 0's name and index 4's, stores 4. T5000 stores
    // what T3000 would, and both show that name.
    int signal_type_from_name(const std::string& name);

    // The two states a digital input on range `range` (1-22) is switched
    // between, as the Value cell shows them: Digital_Units_Array's entry,
    // split at its "/", so "Off" and "On" for range 1. False for any other
    // range.
    bool digital_states(int range, std::string& off, std::string& on);

    // Whether `a` and `b` are the same text but for the case of the letters
    // A-Z, as CString::CompareNoCase compares T3000's names.
    bool same_ignoring_case(const std::string& a, const std::string& b);
}
