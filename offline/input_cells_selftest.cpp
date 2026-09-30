// Tests for how a typed or chosen Value, Calibration or Signal Type becomes
// an input's bytes, as T3000's grid makes them.
//
// That the rules are T3000's is checked against its source by
// conformance/input_cells_guard.cpp. These pin the arithmetic: where T3000
// truncates, where it rounds to a float first, and where the limits fall.

#include "input_cells.h"

#include <string>

#include "../testing/check.h"

namespace
{
    using namespace t5000::offline;
    using namespace t5000::testing;

    bool number_is(const char* text, double expected)
    {
        double value = 0;
        return typed_number(text, value) && value == expected;
    }

    bool refused(const char* text)
    {
        double value = 0;
        return !typed_number(text, value);
    }

    void test_a_typed_number()
    {
        check(number_is("12", 12), "\"12\" is 12");
        check(number_is(" -2.5\t", -2.5), "\" -2.5\" with spaces around it is -2.5");
        check(number_is("+3", 3), "\"+3\" is 3");
        check(number_is(".5", 0.5), "\".5\" is 0.5");
        check(number_is("5.", 5), "\"5.\" is 5");
        check(number_is("1e3", 1000), "\"1e3\" is 1000");
        check(number_is("1E-2", 0.01), "\"1E-2\" is 0.01");
        check(number_is("0.1", 0.1), "\"0.1\" is the double nearest 0.1, as _wtof reads it");

        double minus_zero = 1;
        check(typed_number("-0", minus_zero) && minus_zero == 0, "\"-0\" is 0");

        check(refused(""), "nothing is refused");
        check(refused("  "), "  and nor are spaces");
        check(refused("abc"), "\"abc\" is refused, which _wtof reads as 0");
        check(refused("12abc"), "\"12abc\" is refused, which _wtof reads as 12");
        check(refused("1,5"), "\"1,5\" is refused, which _wtof reads as 1");
        check(refused("inf"), "\"inf\" is refused");
        check(refused("nan"), "\"nan\" is refused");
        check(refused("0x10"), "\"0x10\" is refused, which _wtof reads as 16");
        check(refused("1e"), "\"1e\" is refused");
        check(refused("e3"), "\"e3\" is refused");
        check(refused("."), "\".\" is refused");
        check(refused("-"), "\"-\" is refused");
        check(refused("+-1"), "\"+-1\" is refused");
        check(refused("1.2.3"), "\"1.2.3\" is refused");
        check(refused("1 2"), "\"1 2\" is refused");
        check(refused("1e400"), "\"1e400\", past a double, is refused");
    }

    bool thousandths_are(double typed, long expected)
    {
        int32_t value = 12345;
        return value_thousandths(typed, value) && value == expected;
    }

    bool thousandths_refused(double typed)
    {
        int32_t value = 12345;
        return !value_thousandths(typed, value) && value == 12345;
    }

    void test_a_value()
    {
        check(thousandths_are(1.5, 1500), "1.5 is 1500 thousandths");
        check(thousandths_are(0.001, 1), "0.001 is 1");
        check(thousandths_are(-2.5, -2500), "-2.5 is -2500");
        check(thousandths_are(21.5, 21500), "21.5 is 21500");

        // (int) truncates towards zero, and 1.001 is a little below 1.001.
        check(thousandths_are(1.001, 1000), "1.001 is 1000, not 1001: T3000 truncates");
        check(thousandths_are(-0.0005, 0), "-0.0005 is 0, truncated towards zero");

        check(thousandths_are(2147483.647, 2147483647L), "2147483.647 is INT_MAX");
        check(thousandths_refused(2147483.648), "2147483.648, past INT_MAX, is refused");
        check(thousandths_are(-2147483.648, -2147483647L - 1), "-2147483.648 is INT_MIN");
        check(thousandths_refused(-2147483.649), "-2147483.649, past INT_MIN, is refused");
        check(thousandths_refused(1e300), "1e300 is refused");
    }

    bool calibration_is(double typed, int sign, int high, int low)
    {
        CalibrationBytes b;
        b.sign = 9;
        return calibration_bytes(typed, b) && b.sign == sign && b.high == high && b.low == low;
    }

    bool calibration_refused(double typed)
    {
        CalibrationBytes b;
        b.sign = 9;
        return !calibration_bytes(typed, b) && b.sign == 9;
    }

    void test_a_calibration()
    {
        check(calibration_is(2.5, 0, 0, 25), "2.5 is plus, 25 tenths");
        check(calibration_is(-2.5, 1, 0, 25), "-2.5 is minus, 25 tenths");
        check(calibration_is(25.6, 0, 1, 0), "25.6 is 256 tenths: high byte 1, low byte 0");
        check(calibration_is(1000, 0, 39, 16), "1000 is 10000 tenths: 39 and 16");
        check(calibration_is(6553.5, 0, 255, 255), "6553.5 is 65535 tenths, the most there is");
        check(calibration_is(-6553.5, 1, 255, 255), "-6553.5 is minus 65535 tenths");
        check(calibration_refused(6553.6), "6553.6 is refused, as T3000 refuses it");
        check(calibration_refused(-6553.6), "  and -6553.6");
        check(calibration_refused(1.0e7), "1e7 is refused");
        check(calibration_refused(-1.0e300), "-1e300 is refused");

        // The float rounding T3000's build does. 0.7 as a float is a little
        // below 0.7, and times 10 a little below 7, which rounds to 7.0 as a
        // float: 6 if the product were kept as a double.
        check(calibration_is(0.7, 0, 0, 7), "0.7 is 7 tenths: the product is rounded to a float");
        check(calibration_is(0.15, 0, 0, 1), "0.15 is 1 tenth: truncated");
        check(calibration_is(1.15, 0, 0, 11), "1.15 is 11 tenths");
        // 6553.59999 as a float is 6553.60009765625, whose product is 65536:
        // refused, where a double throughout would give 65535.
        check(calibration_refused(6553.59999), "6553.59999 is refused: as a float it is past 6553.6");
        check(calibration_is(6553.599, 0, 255, 255), "6553.599 is 65535 tenths");

        // The sign follows the number typed.
        check(calibration_is(-0.04, 1, 0, 0), "-0.04 is minus, 0 tenths: below zero, however little");
        check(calibration_is(-0.0, 0, 0, 0), "-0 is plus: it is not below zero");
        check(calibration_is(0, 0, 0, 0), "0 is plus, 0 tenths");
    }

    void test_a_signal_type()
    {
        const std::vector<std::string> choices = signal_type_choices();
        check_eq((long)choices.size(), 5, "five signal types are offered");
        if (choices.size() == 5)
        {
            check(choices[0] == "Thermistor Dry Contact", "  Thermistor Dry Contact");
            check(choices[1] == "4-20 ma", "  4-20 ma");
            check(choices[2] == "0-5 V", "  0-5 V");
            check(choices[3] == "0-10 V", "  0-10 V");
            check(choices[4] == "PT 1K", "  and PT 1K: index 4, the second Thermistor Dry Contact, is left out");
        }

        check_eq(signal_type_from_name("Thermistor Dry Contact"), 4,
                 "Thermistor Dry Contact stores 4: the last of JumperStatus's names that matches");
        check_eq(signal_type_from_name("thermistor DRY contact"), 4, "  whatever the case of its letters");
        check_eq(signal_type_from_name("4-20 ma"), 1, "4-20 ma stores 1");
        check_eq(signal_type_from_name("4-20 MA"), 1, "  and so does 4-20 MA");
        check_eq(signal_type_from_name("0-5 V"), 2, "0-5 V stores 2");
        check_eq(signal_type_from_name("0-10 v"), 3, "0-10 v stores 3");
        check_eq(signal_type_from_name("PT 1K"), 5, "PT 1K stores 5");
        check_eq(signal_type_from_name("4-20 ma / 0-24 V"), -1, "a name that is not one of them is none");
        check_eq(signal_type_from_name(" 0-5 V"), -1, "  nor is one with a space before it");
        check_eq(signal_type_from_name(""), -1, "  nor is nothing");
    }

    void test_a_digital_state()
    {
        std::string off, on;
        check(digital_states(1, off, on) && off == "Off" && on == "On", "range 1 is Off or On");
        check(digital_states(10, off, on) && off == "Unoccupy" && on == "Occupy", "range 10 is Unoccupy or Occupy");
        check(digital_states(22, off, on) && off == "High" && on == "Low", "range 22 is High or Low");
        check(!digital_states(0, off, on), "range 0 has no states");
        check(!digital_states(23, off, on), "  nor does 23, the device's first custom range");
        check(!digital_states(-1, off, on), "  nor -1");

        bool all = true;
        for (int r = 1; r <= 22; r++)
            all = all && digital_states(r, off, on) && !off.empty() && !on.empty() && off != on;
        check(all, "each of ranges 1-22 has two states, and they differ");
    }

    void test_same_ignoring_case()
    {
        check(same_ignoring_case("Off", "oFF"), "\"Off\" is \"oFF\"");
        check(!same_ignoring_case("Off", "Of"), "\"Off\" is not \"Of\"");
        check(!same_ignoring_case("On", "Om"), "\"On\" is not \"Om\"");
        check(!same_ignoring_case("\xC3\xA9", "\xC3\x89"), "only A-Z are folded, as in the C locale");
    }
}

int run_input_cells_tests()
{
    test_a_typed_number();
    test_a_value();
    test_a_calibration();
    test_a_signal_type();
    test_a_digital_state();
    test_same_ignoring_case();
    return 0;
}
