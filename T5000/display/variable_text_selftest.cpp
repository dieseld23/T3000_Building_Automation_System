// Tests for what a variable shows, against Fresh_Variable_List
// (BacnetVariable.cpp:209-478) and intervaltotextfull
// (global_function.cpp:7856). An empty note means "exactly as T3000", so
// every row T3000 writes in full is checked to have none.

#include "variable_text.h"
#include "tables.h"
#include "../testing/check.h"

#include <string.h>

#include <vector>

namespace
{
    using namespace t5000::display;
    using namespace t5000::testing;
    using t5000::wire::VariablePoint;
    namespace w = t5000::wire;

    VariablePoint analog(uint8_t range, int32_t value = 0)
    {
        VariablePoint p{};
        p.digital_analog = 1;
        p.range          = range;
        p.value          = value;
        return p;
    }

    VariablePoint digital(uint8_t range, uint8_t control, int32_t value = 0)
    {
        VariablePoint p{};
        p.digital_analog = 0;
        p.range          = range;
        p.control        = control;
        p.value          = value;
        return p;
    }

    const VariablePanel kNothingRead;

    bool has(const std::string& text, const char* part)
    {
        return text.find(part) != std::string::npos;
    }

    // A panel whose multi-state tables `first`-`last` were asked for, and
    // `read` of them came back: table 0 names 0 Off, 1 Cool, 2 Heat; table
    // 1 names 1 Pump B; table 3 names 1 Four.
    VariablePanel with_tables(int asked, int read)
    {
        std::vector<uint8_t> b((size_t)read * w::kMsvTableWireSize, 0);
        const auto item = [&b](int t, int k, const char* name, uint16_t value)
        {
            if (t >= (int)(b.size() / w::kMsvTableWireSize))
                return;
            uint8_t* at = &b[(size_t)t * w::kMsvTableWireSize + (size_t)k * w::kMsvItemWireSize];
            at[w::msv_item_at::status] = 1;
            memcpy(at + w::msv_item_at::name, name, strlen(name));
            at[w::msv_item_at::value] = (uint8_t)value;
        };
        item(0, 0, "Off", 0);
        item(0, 1, "Cool", 1);
        item(0, 2, "Heat", 2);
        item(1, 0, "Pump B", 1);
        item(3, 0, "Four", 1);

        VariablePanel panel;
        for (int t = 0; t < asked; t++)
            panel.names.msv_asked[t] = true;
        if (read > 0)
            take_msv_tables(b.data(), b.size(), 0, read, panel.names);
        return panel;
    }

    void test_the_labels_and_auto_manual()
    {
        section("Full Label and Label trimmed, and Auto or Manual");

        VariablePoint p = analog(1, 21500);
        memcpy(p.description, "  Zone Setpoint  ", 17);
        memcpy(p.label, " ZN_SP ", 7);
        VariableText t = variable_text(p, kNothingRead);
        check(t.full_label == "Zone Setpoint" && t.label == "ZN_SP", "both trimmed");
        check(t.auto_manual == "Auto", "auto_manual 0: Auto");
        p.auto_manual = 1;
        check(variable_text(p, kNothingRead).auto_manual == "Manual", "1: Manual");
        p.auto_manual = 7;
        check(variable_text(p, kNothingRead).auto_manual == "Manual", "anything else: Manual");
    }

    void test_numbers()
    {
        section("a variable's number: value / 1000, three decimals, in float");

        check(variable_number(21500) == "21.500", "21500 is 21.500");
        check(variable_number(-1234) == "-1.234", "negative values keep their sign");
        check(variable_number(0) == "0.000", "zero is 0.000");
        check(variable_number(1) == "0.001", "1 is 0.001");

        // Far enough from 0 that a float cannot hold the thousandths.
        char in_double[32];
        snprintf(in_double, sizeof(in_double), "%.3f", 123456700 / 1000.0);
        check(strcmp(in_double, "123456.700") == 0, "in double, 123456700 is 123456.700");
        check(variable_number(123456700) == "123456.703", "but T3000 shows 123456.703, and so does T5000");
    }

    void test_times()
    {
        section("a time, as intervaltotextfull writes it");

        check(interval_text(0) == "00:00:00", "0: 00:00:00");
        check(interval_text(59) == "00:00:59", "59 s");
        check(interval_text(5430) == "01:30:30", "5430 s: 01:30:30");
        check(interval_text(86399) == "23:59:59", "a day less a second");
        check(interval_text(86400) == "24:00:00", "a day: 24 hours, not a day");
        check(interval_text(2147483) == "596:31:23", "the most a value can give: three digits of hours");
        check(interval_text(-1) == "-00:00:01", "a negative time has a minus sign");
        check(interval_text(-2147483) == "-596:31:23", "  the most negative too");

        // In the grid, whole seconds by integer division.
        VariableText t = variable_text(analog(20, 5430000), kNothingRead);
        check(t.value == "01:30:30" && t.units == "Time" && t.note.empty(), "an analog variable on range 20");
        check(variable_text(analog(20, -999), kNothingRead).value == "00:00:00",
              "-999 is 0 seconds, so no minus sign");
        check(variable_text(analog(20, -1000), kNothingRead).value == "-00:00:01", "-1000 is -1 second");
        check(variable_text(analog(20, 1999), kNothingRead).value == "00:00:01", "1999 is 1 second, cut, not rounded");
        check(variable_text(digital(20, 1), kNothingRead).value == "Cool", "a digital one on 20 is a state pair");
    }

    void test_analog_units()
    {
        section("an analog variable's units, by range");

        VariableText t = variable_text(analog(1, 21500), kNothingRead);
        check(t.value == "21.500" && t.units == "\xC2\xB0" "C" && t.note.empty(), "range 1: degrees C");
        check(variable_text(analog(0, 5), kNothingRead).units == "Unused", "range 0: Unused, and a number");
        check(variable_text(analog(0, 5), kNothingRead).value == "0.005", "  the number");
        check(variable_text(analog(33), kNothingRead).units == "m\xC2\xB3/h", "range 33, the last fixed unit");
        check(variable_text(analog(39), kNothingRead).units == "Unused", "range 39: Unused");
        check(variable_text(analog(100), kNothingRead).units == "Unused", "range 100: Unused");
        check(variable_text(analog(105), kNothingRead).units == "Unused", "range 105: Unused");
        check(variable_text(analog(255, 1000), kNothingRead).value == "1.000", "  each with its number");

        VariablePoint two = analog(2);
        two.digital_analog = 2;
        check(variable_text(two, kNothingRead).units == "\xC2\xB0" "F", "digital_analog 2 is analog: T3000 tests for 0");
    }

    void test_custom_units()
    {
        section("an analog variable on 34-38 takes the device's own units");

        VariablePanel panel;
        std::vector<uint8_t> b(5 * w::kVariableUnitWireSize, 0);
        memcpy(&b[0], "L/s", 3);        // unit 1
        memcpy(&b[80], "gal/h", 5);     // unit 5; 2-4 empty
        take_variable_units(b.data(), b.size(), 0, 5, panel.names);

        VariableText t = variable_text(analog(34, 12750), panel);
        check(t.units == "L/s" && t.value == "12.750" && t.note.empty(), "range 34: unit 1, exactly as T3000");
        check(variable_text(analog(38), panel).units == "gal/h", "range 38: unit 5");
        check(variable_text(analog(35), panel).units.empty() && variable_text(analog(35), panel).note.empty(),
              "an empty name read: empty, as T3000 shows it");

        t = variable_text(analog(36, 1000), kNothingRead);
        check(t.units == "custom unit 3" && t.value == "1.000", "not read: named by its number");
        check(has(t.note, "custom unit 3") && has(t.note, "did not send it"), "  and the note says so");
    }

    void test_digital_numbers()
    {
        section("a digital variable on range 0 or 31-99 is a number, Unused");

        VariableText t = variable_text(digital(0, 1, 1500), kNothingRead);
        check(t.value == "1.500" && t.units == "Unused" && t.note.empty(), "range 0");
        check(variable_text(digital(31, 0, 2000), kNothingRead).value == "2.000", "range 31");
        check(variable_text(digital(99, 0, 3000), kNothingRead).value == "3.000", "range 99");
    }

    void test_digital_states()
    {
        section("a digital variable on 1-22 is a state of its pair");

        VariableText t = variable_text(digital(1, 0), kNothingRead);
        check(t.value == "Off" && t.units == "Off/On" && t.note.empty(), "range 1, control 0: Off, Off/On");
        check(variable_text(digital(1, 1), kNothingRead).value == "On", "control 1: On");
        check(variable_text(digital(1, 9), kNothingRead).value == "On", "any other control: the second");
        check(variable_text(digital(22, 0), kNothingRead).units == "High/Low", "range 22, the last fixed");
    }

    void test_custom_digital_ranges()
    {
        section("a digital variable on 23-30 takes the device's own state names");

        VariablePanel named;
        named.ranges.digital_known = true;
        DigitalRange& horn = named.ranges.digital[0];
        horn.text       = "Quiet/Sound";
        horn.has_states = true;
        horn.off        = "Quiet";
        horn.on         = "Sound";
        named.ranges.digital[7].text = "one/two/three";

        VariableText t = variable_text(digital(23, 1), named);
        check(t.value == "Sound" && t.units == "Quiet/Sound" && t.note.empty(), "range 23: range 1's names");
        check(variable_text(digital(23, 0), named).value == "Quiet", "  control 0: the first");

        t = variable_text(digital(30, 1), named);
        check(t.units == "one/two/three" && t.value.empty(), "names that do not split in two: no value");
        check(has(t.note, "writes neither Value nor Units"), "  and T3000 writes neither cell, as the note says");

        t = variable_text(digital(24, 1), kNothingRead);
        check(t.units == "custom range 2" && t.value == "1", "not read: named by its number, the state as 1");
        check(variable_text(digital(24, 0), kNothingRead).value == "0", "  or 0");
        check(has(t.note, "writes neither Value nor Units") && !has(t.note, "another row"),
              "  and the note says T3000 writes neither - nothing is left over from another row");
    }

    void test_digital_ranges_without_a_value()
    {
        section("a digital variable on 100 or 105 up shows Unused and no value");

        for (int range : { 100, 105, 200, 255 })
        {
            const VariableText t = variable_text(digital((uint8_t)range, 1, 1000), kNothingRead);
            check(t.units == "Unused" && t.value.empty() && has(t.note, "keeps the text it had"),
                  ("range " + std::to_string(range)).c_str());
        }
    }

    void test_multi_state()
    {
        section("a variable on 101-104 is named by a multi-state table");

        const VariablePanel panel = with_tables(3, 3);   // firmware 60.7: tables 0-2, all read

        VariableText t = variable_text(digital(101, 0, 2000), panel);
        check(t.value == "Heat" && t.units == "Off / Cool / Heat /..." && t.note.empty(),
              "range 101, value 2000: Heat, in table 1's name");
        check(variable_text(analog(101, 1000), panel).value == "Cool", "an analog one alike");
        check(variable_text(digital(101, 0, 2999), panel).value == "Heat", "2999 looks up 2: cut, not rounded");
        check(variable_text(digital(101, 0, -500), panel).value == "Off", "-500 looks up 0: cut toward zero");

        // In float, 16999999 is 17000000, so T3000 looks up 17000 where
        // double arithmetic would look up 16999.
        std::vector<uint8_t> big(w::kMsvTableWireSize, 0);
        big[w::msv_item_at::status] = 1;
        memcpy(&big[w::msv_item_at::name], "Seventeen", 9);
        big[w::msv_item_at::value]     = (uint8_t)(17000 & 0xFF);
        big[w::msv_item_at::value + 1] = (uint8_t)(17000 >> 8);
        VariablePanel wide = panel;
        take_msv_tables(big.data(), big.size(), 0, 1, wide.names);
        check(variable_text(digital(101, 0, 16999999), wide).value == "Seventeen",
              "16999999 looks up 17000, as the float T3000 computes in rounds it");
        check(variable_text(digital(101, 0, 7000), panel).value == "7.000", "a value no item names: the number");
        check(variable_text(digital(102, 0, 1000), panel).value == "Pump B", "range 102: table 2");

        t = variable_text(digital(104, 0, 1000), panel);
        check(t.value == "1.000" && t.units.empty() && t.note.empty(),
              "range 104 on firmware 60.7: table 4 never asked for, so no name, and no units");

        const VariablePanel four = with_tables(4, 4);
        t = variable_text(digital(104, 0, 1000), four);
        check(t.units == "Four" && t.value == "1.000" && t.note.empty(),
              "range 104 on newer firmware: table 4's name, but the value a number - T3000 looks in 1-3 only");
    }

    void test_multi_state_tables_not_all_read()
    {
        section("multi-state tables that did not all come back");

        const VariablePanel partly = with_tables(4, 2);   // 0-3 asked for, 0-1 came back
        VariableText t = variable_text(digital(101, 0, 1000), partly);
        check(t.value == "Cool" && t.units == "Off / Cool / Heat /...", "a table that came back names its states");
        check(has(t.note, "T3000 writes no Units"), "  but T3000 writes no Units, as the note says");

        t = variable_text(digital(103, 0, 1000), partly);
        check(t.units == "multi-state table 3" && t.value == "1.000", "one that did not: named by its number");
        check(has(t.note, "table 3, which did not come back"), "  and the note says so");

        t = variable_text(digital(101, 0, 1000), kNothingRead);
        check(t.value == "1.000" && t.units.empty() && has(t.note, "T3000 writes no Units"),
              "none asked for: the number, and no units");
    }
}

int run_variable_text_tests()
{
    test_the_labels_and_auto_manual();
    test_numbers();
    test_times();
    test_analog_units();
    test_custom_units();
    test_digital_numbers();
    test_digital_states();
    test_custom_digital_ranges();
    test_digital_ranges_without_a_value();
    test_multi_state();
    test_multi_state_tables_not_all_read();
    return 0;
}
