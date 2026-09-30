// Tests for what an input shows, against what BacnetInput.cpp:951-1237 does.

#include "input_text.h"
#include "tables.h"
#include "../testing/check.h"

namespace
{
    using namespace t5000::display;
    using namespace t5000::testing;
    using t5000::wire::InputPoint;

    InputPoint analog_point(uint8_t range, int32_t value = 0)
    {
        InputPoint p{};
        p.digital_analog = 1;
        p.range          = range;
        p.value          = value;
        return p;
    }

    InputPoint digital_point(uint8_t range, uint8_t control, int32_t value = 0)
    {
        InputPoint p{};
        p.digital_analog = 0;
        p.range          = range;
        p.control        = control;
        p.value          = value;
        return p;
    }

    const PanelContext kUnknownPanel;

    void test_values_are_thousandths_rounded_in_float()
    {
        section("an analog value is value/1000 to two places, computed in float as T3000 does");

        check(thousandths(21500) == "21.50", "21500 is 21.50");
        check(thousandths(-4250) == "-4.25", "negative values keep their sign");
        check(thousandths(0) == "0.00", "zero is 0.00");

        // 0.045 is just below itself in double and just above in float, and
        // 0.055 the other way round, so the two disagree at two decimals.
        // T3000 divides in float. (12.345 would not do: it is above .345 in
        // both.)
        char in_double[16];
        snprintf(in_double, sizeof(in_double), "%.2f", 45 / 1000.0);
        check(strcmp(in_double, "0.04") == 0, "in double, 45 rounds to 0.04");
        check(thousandths(45) == "0.05", "but T3000 shows 0.05, and so does T5000");

        snprintf(in_double, sizeof(in_double), "%.2f", 55 / 1000.0);
        check(strcmp(in_double, "0.06") == 0, "in double, 55 rounds to 0.06");
        check(thousandths(55) == "0.05", "but in float it is 0.05");

        const InputText t = input_text(analog_point(3, 21500), 0, kUnknownPanel);
        check(t.value == "21.50", "and that is the Value column");
    }

    void test_an_analog_input_shows_units_and_range_by_range()
    {
        section("an analog input's units and range come from its range byte");

        const InputText temp = input_text(analog_point(3, 21500), 0, kUnknownPanel);
        check(temp.units == "\xC2\xB0" "C", "range 3 is in degrees C");
        check(temp.range == "10K Type2", "on a 10K Type2 sensor");
        check(temp.note.empty(), "exactly as T3000 shows it");

        const InputText volts = input_text(analog_point(19), 0, kUnknownPanel);
        check(volts.units == "Volts", "range 19 is Volts - the entry after the three commented out");

        const InputText amps = input_text(analog_point(36), 0, kUnknownPanel);
        check(amps.units == "A", "range 36 is A");
        check(amps.range == "-200A to 200A", "  on -200A to 200A");

        const InputText unused = input_text(analog_point(0), 0, kUnknownPanel);
        check(unused.range == "Unused", "range 0 is Unused");
        check(unused.units.empty(), "  with no units");
    }

    void test_analog_ranges_past_the_tables()
    {
        section("analog ranges past the end of each table");

        // Both tables have 40 entries, so 39 is the last inside them and 40
        // the first outside. T3000 bounds these by sizeof, not by a number.
        const InputText last = input_text(analog_point((uint8_t)(count(kInputAnalogRanges) - 1)), 0, kUnknownPanel);
        check(last.range == kInputAnalogRanges[count(kInputAnalogRanges) - 1],
              "the last range in the table shows its entry");

        const InputText past = input_text(analog_point((uint8_t)count(kInputAnalogRanges)), 0, kUnknownPanel);
        check(past.range == "Out of range", "one past it is Out of range");
        check(past.units.empty(), "  with no units");
    }

    void test_custom_analog_ranges_say_they_are_custom()
    {
        section("an analog input on a custom table says its units are on the device");

        const InputText t = input_text(analog_point(22, 1500), 0, kUnknownPanel);
        check(t.range == "Table 3", "range 22 is Table 3, as T3000 names it");
        check(t.units.empty(), "the unit name is not known");
        check(t.note.find("custom table 3") != std::string::npos, "and the note says which table");
        check(t.value == "1.50", "the value is still shown");
    }

    void test_calibration_is_tenths_with_a_separate_sign()
    {
        section("calibration is an unsigned count of tenths, with the sign in its own column");

        InputPoint p = analog_point(3);
        p.calibration_h    = 0x01;
        p.calibration_l    = 0x2C;   // 300 tenths
        p.calibration_sign = 1;
        const InputText t = input_text(p, 0, kUnknownPanel);
        check(t.calibration == "30.0", "0x012C is 30.0");
        check(t.sign == "-", "and sign 1 is -");

        p.calibration_sign = 0;
        check(input_text(p, 0, kUnknownPanel).sign == "+", "sign 0 is +");

        p.calibration_h = 0xFF;
        p.calibration_l = 0xFF;
        check(input_text(p, 0, kUnknownPanel).calibration == "6553.5",
              "unsigned: 0xFFFF is 6553.5, not -0.1");
    }

    void test_a_digital_input_shows_its_state_from_control()
    {
        section("a digital input's state comes from control, not from value");

        const InputText off = input_text(digital_point(1, 0, 1000), 0, kUnknownPanel);
        check(off.range == "Off/On", "range 1 is Off/On");
        check(off.value == "Off", "control 0 is the first name, whatever value says");

        const InputText on = input_text(digital_point(1, 1, 0), 0, kUnknownPanel);
        check(on.value == "On", "control 1 is the second, with value 0");

        const InputText reversed = input_text(digital_point(12, 1), 0, kUnknownPanel);
        check(reversed.value == "Off", "range 12 is On/Off, so control 1 is Off");

        const InputText last = input_text(digital_point(22, 0), 0, kUnknownPanel);
        check(last.value == "High", "range 22, the last fixed one, is High/Low");

        check(off.units.empty() && off.calibration.empty() && off.sign.empty(),
              "no units or calibration on a digital point");
    }

    void test_a_digital_input_on_range_zero_shows_its_value()
    {
        section("a digital input on range 0 shows its raw value");

        const InputText t = input_text(digital_point(0, 1, 2500), 0, kUnknownPanel);
        check(t.value == "2.50", "value/1000, as for analog");
        check(t.range == "Unused", "and the range is Unused");
    }

    void test_custom_digital_ranges_show_the_state_number()
    {
        section("a digital input on a custom range shows 0 or 1, and says why");

        const InputText t = input_text(digital_point(23, 1), 0, kUnknownPanel);
        check(t.value == "1", "control 1 is shown as 1");
        check(t.range == "custom range 1", "range 23 is the first custom range");
        check(t.note.find("custom digital range 1") != std::string::npos, "and the note explains");

        check(input_text(digital_point(30, 0), 0, kUnknownPanel).range == "custom range 8",
              "range 30 is the eighth");

        const InputText beyond = input_text(digital_point(31, 1), 0, kUnknownPanel);
        check(beyond.range == "Unused", "range 31 is past the custom ones: Unused");
        check(beyond.value.empty(), "with no value, as T3000 shows none");
        check(!beyond.note.empty(), "and a note saying so");
    }

    PanelContext with_names()
    {
        PanelContext c;
        c.ranges.digital_known = true;
        c.ranges.digital[0]    = { "Closed/Tripped", true, "Closed", "Tripped" };
        c.ranges.digital[7]    = { "A/B/C", false, "", "" };
        c.ranges.analog_known[2] = true;
        c.ranges.analog[2]       = "kPa";
        return c;
    }

    void test_custom_ranges_use_the_devices_names()
    {
        section("a custom range shows the names the device sent");

        const PanelContext names = with_names();

        const InputText closed = input_text(digital_point(23, 0), 0, names);
        check(closed.range == "Closed/Tripped", "range 23 shows custom digital range 1 in full");
        check(closed.value == "Closed", "control 0 is its first state");
        check(closed.note.empty(), "exactly as T3000 shows it");
        check(input_text(digital_point(23, 1), 0, names).value == "Tripped", "control 1 is its second");

        const InputText three = input_text(digital_point(30, 1), 0, names);
        check(three.range == "A/B/C", "a range whose names do not split in two is still named");
        check(three.value.empty(), "but shows no value, as T3000 shows none");
        check(!three.note.empty(), "and says why");

        const InputText kpa = input_text(analog_point(22, 1500), 0, names);
        check(kpa.units == "kPa", "range 22 takes custom table 3's name as its units");
        check(kpa.range == "Table 3", "while the range keeps T3000's own name for it");
        check(kpa.note.empty(), "with nothing to explain");

        const InputText missing = input_text(analog_point(23), 0, names);
        check(missing.units.empty() && !missing.note.empty(), "a table the device did not send has a note");

        PanelContext digital_unknown = names;
        digital_unknown.ranges.digital_known = false;
        check(input_text(digital_point(23, 1), 0, digital_unknown).value == "1",
              "names stored but not known complete: T3000 does not use them, so 0 or 1");
    }

    void test_every_fixed_digital_range_splits_in_two()
    {
        section("the SplitCStringA port gives every fixed digital range its two states");

        // The fixed ranges were split on their one slash before; they now go
        // through the same port as the device's names. Each must still come
        // out as the text either side of that slash.
        for (int range = 1; range <= 22; range++)
        {
            const std::string pair = kDigitalUnits[range];
            const size_t slash = pair.find('/');
            const InputText off = input_text(digital_point((uint8_t)range, 0), 0, kUnknownPanel);
            const InputText on  = input_text(digital_point((uint8_t)range, 1), 0, kUnknownPanel);
            const std::string what = "range " + std::to_string(range) + " (" + pair + ")";
            check(off.value == pair.substr(0, slash) && on.value == pair.substr(slash + 1), what.c_str());
        }
    }

    void test_status_is_the_low_nibble_unless_the_range_rules_it_out()
    {
        section("status: open and short alarms, except where the range makes them meaningless");

        InputPoint p = analog_point(3);

        p.decom = 0x00;
        check(input_text(p, 0, kUnknownPanel).status == "Normal", "0 is Normal");

        p.decom = 0x01;
        InputText t = input_text(p, 0, kUnknownPanel);
        check(t.status == "Open" && t.alarm, "1 is Open, and an alarm");

        p.decom = 0x02;
        t = input_text(p, 0, kUnknownPanel);
        check(t.status == "Shorted" && t.alarm, "2 is Shorted, and an alarm");

        p.decom = 0x03;
        t = input_text(p, 0, kUnknownPanel);
        check(t.status.empty() && !t.alarm, "3 shows nothing");

        p.decom = 0x01;
        p.range = 0;
        t = input_text(p, 0, kUnknownPanel);
        check(t.status == "Normal" && !t.alarm, "an open circuit on range 0 is not reported");

        p.range = 40;   // > 30 and not 101-104
        check(input_text(p, 0, kUnknownPanel).status == "Normal", "nor on an invalid range");

        p.range = 101;  // multi-state: valid
        check(input_text(p, 0, kUnknownPanel).status == "Open", "but 101 is a valid range, so it is");
    }

    void test_signal_type_is_the_high_nibble()
    {
        section("signal type is the high nibble, and an unknown one shows the first");

        InputPoint p = analog_point(3);
        p.decom = 0x10;
        check(input_text(p, 0, kUnknownPanel).signal_type == "4-20 ma", "1 is 4-20 ma");
        p.decom = 0x50;
        check(input_text(p, 0, kUnknownPanel).signal_type == "PT 1K", "5 is PT 1K, the last");

        p.decom = 0x61;
        const InputText t = input_text(p, 0, kUnknownPanel);
        check(t.signal_type == "Thermistor Dry Contact", "6 is past the table: the first entry");
        check(t.status == "Open", "and the status nibble is still read");
        check(p.decom == 0x61, "the point itself is not altered, as T3000 alters its copy");
    }

    void test_invalid_range_matches_t3000()
    {
        section("bac_Invalid_range");

        check(!is_invalid_range(30), "30 is valid");
        check(is_invalid_range(31), "31 is not");
        check(!is_invalid_range(101) && !is_invalid_range(104), "101-104 are");
        check(is_invalid_range(100) && is_invalid_range(105), "100 and 105 are not");
    }

    void test_an_rmc1232_labels_inputs_9_to_12_only_when_known()
    {
        section("the RMC1232's own range names need its panel type, from its settings");

        const InputText unknown = input_text(analog_point(11), 8, kUnknownPanel);
        check(unknown.range == "0.0 to 5.0", "panel type unknown: the table's entry");

        PanelContext rmc;
        rmc.known = true;
        rmc.type  = t5000::device::MiniType::Rmc1232;
        check(input_text(analog_point(11), 8, rmc).range == "-30V to -65V", "IN9 on an RMC1232");
        check(input_text(analog_point(11), 11, rmc).range == "0 to 30V", "IN12 on an RMC1232");
        check(input_text(analog_point(11), 7, rmc).range == "0.0 to 5.0", "IN8 is not special");
    }

    void test_auto_manual_and_filter()
    {
        section("auto/manual and filter");

        InputPoint p = analog_point(3);
        p.auto_manual = 1;
        p.filter      = 200;
        const InputText t = input_text(p, 0, kUnknownPanel);
        check(t.auto_manual == "Manual", "1 is Manual");
        check(t.filter == "200", "filter as an unsigned number");
    }
}

int run_input_text_tests()
{
    test_values_are_thousandths_rounded_in_float();
    test_an_analog_input_shows_units_and_range_by_range();
    test_analog_ranges_past_the_tables();
    test_custom_analog_ranges_say_they_are_custom();
    test_calibration_is_tenths_with_a_separate_sign();
    test_a_digital_input_shows_its_state_from_control();
    test_a_digital_input_on_range_zero_shows_its_value();
    test_custom_digital_ranges_show_the_state_number();
    test_custom_ranges_use_the_devices_names();
    test_every_fixed_digital_range_splits_in_two();
    test_status_is_the_low_nibble_unless_the_range_rules_it_out();
    test_signal_type_is_the_high_nibble();
    test_invalid_range_matches_t3000();
    test_an_rmc1232_labels_inputs_9_to_12_only_when_known();
    test_auto_manual_and_filter();
    return 0;
}
