// Tests for what an output shows, against what BacnetOutput.cpp:658-1141
// does.

#include "output_text.h"
#include "tables.h"
#include "../testing/check.h"

#include <string.h>

namespace
{
    using namespace t5000::display;
    using namespace t5000::testing;
    using t5000::device::ProductClassId;
    using t5000::wire::OutputPoint;

    OutputPoint analog_point(uint8_t range, int32_t value = 0)
    {
        OutputPoint p{};
        p.digital_analog   = 1;
        p.range            = range;
        p.value            = value;
        p.hw_switch_status = 1;
        return p;
    }

    OutputPoint digital_point(uint8_t range, uint8_t control, int32_t value = 0)
    {
        OutputPoint p{};
        p.digital_analog   = 0;
        p.range            = range;
        p.control          = control;
        p.value            = value;
        p.hw_switch_status = 1;
        return p;
    }

    // A panel whose settings were read, of this bacnet_device_type.
    OutputPanel panel_of(int type)
    {
        OutputPanel panel;
        panel.known = true;
        panel.type  = type;
        panel.rows  = t5000::device::output_rows_for(type, ProductClassId::MiniPanelArm, t5000::wire::PanelSettings());
        return panel;
    }

    const OutputPanel kUnknownPanel;

    constexpr int kCm5          = 0;
    constexpr int kBigMiniPanel = 1;    // HOA, 12 digital and 12 analog switched
    constexpr int kOem          = 11;   // HOA, none switched
    constexpr int kT38AI8AO6DO  = 44;   // HOA, 6 and 8 switched, no external outputs

    void set(uint8_t* field, size_t length, const char* text)
    {
        memset(field, 0, length);
        memcpy(field, text, strlen(text) < length ? strlen(text) : length);
    }

    void test_the_plain_columns()
    {
        section("labels, voltages, PWM period and status, as T3000 writes them");

        OutputPoint p = analog_point(1);
        set(p.description, sizeof(p.description), "  Supply Fan  ");
        set(p.label, sizeof(p.label), " SF_1 ");
        OutputText t = output_text(p, 0, kUnknownPanel);
        check(t.full_label == "Supply Fan", "the full label is trimmed");
        check(t.label == "SF_1", "and so is the label");

        // A description that fills all 19 bytes - which the decoder keeps
        // only when the byte after it is 0 - shows all 19.
        memset(p.description, 'D', sizeof(p.description));
        p.low_voltage = 0;
        t = output_text(p, 0, kUnknownPanel);
        check(t.full_label == std::string(19, 'D'), "a full 19-character description is shown whole");

        p.low_voltage  = 0;
        p.high_voltage = 25;
        t = output_text(p, 0, kUnknownPanel);
        check(t.low_voltage.empty(), "a voltage of 0 is blank");
        check(t.high_voltage == "2.5", "25 tenths is 2.5");
        p.low_voltage  = 1;
        p.high_voltage = 120;
        t = output_text(p, 0, kUnknownPanel);
        check(t.low_voltage == "0.1" && t.high_voltage == "12.0", "1 is 0.1, 120 is 12.0");

        p.pwm_period = 0;
        check(output_text(p, 0, kUnknownPanel).pwm_period == "0", "a PWM period of 0 is 0, not blank");
        p.pwm_period = 255;
        check(output_text(p, 0, kUnknownPanel).pwm_period == "255", "255 is 255, unsigned");

        p.decom = 0;
        check(output_text(p, 0, kUnknownPanel).status == "OK", "decom 0 is OK");
        p.decom = 1;
        check(output_text(p, 0, kUnknownPanel).status == "-", "decom 1 is -");
        p.decom = 2;
        check(output_text(p, 0, kUnknownPanel).status.empty(), "decom 2 is nothing");
    }

    void test_an_analog_output()
    {
        section("an analog output's range, units and value");

        OutputText t = output_text(analog_point(0, 5000), 0, kUnknownPanel);
        check(t.range == "Unused" && t.units.empty(), "range 0: Unused, and no units");
        check(t.value == "5.00", "and the value still shown");

        t = output_text(analog_point(1, 7350), 0, kUnknownPanel);
        check(t.range == "0.0 -> 10" && t.units == "Volts", "range 1: 0.0 -> 10, Volts");
        check(t.value == "7.35", "value / 1000 to two places");

        t = output_text(analog_point(3), 0, kUnknownPanel);
        check(t.range == "4   -> 20" && t.units == "psi", "range 3: 4   -> 20, psi - three spaces");

        t = output_text(analog_point(8), 0, kUnknownPanel);
        check(t.range == "0.0 -> 100" && t.units == "%", "range 8, the last: 0.0 -> 100, %");

        t = output_text(analog_point(9, -1500), 0, kUnknownPanel);
        check(t.range == "Out of range" && t.units == "Unused", "range 9: Out of range, and units Unused");
        check(t.value == "-1.50", "negative values keep their sign");
        check(t.note.empty(), "all of that is what T3000 shows");
    }

    void test_a_digital_output()
    {
        section("a digital output's range and state");

        OutputText t = output_text(digital_point(0, 1, 1000), 0, kUnknownPanel);
        check(t.range == "Unused" && t.value == "1.00", "range 0: Unused, and the value as a number");
        check(t.units.empty(), "no units");

        t = output_text(digital_point(1, 0), 0, kUnknownPanel);
        check(t.range == "Off/On" && t.value == "Off", "range 1, control 0: Off");
        t = output_text(digital_point(1, 1), 0, kUnknownPanel);
        check(t.value == "On", "control 1: On");
        t = output_text(digital_point(22, 7), 0, kUnknownPanel);
        check(t.range == "High/Low" && t.value == "Low", "range 22, any non-zero control: the second state");

        t = output_text(digital_point(31, 1), 0, kUnknownPanel);
        check(t.range == "Unused" && t.value.empty(), "range 31: Unused, and no value");
        check(!t.note.empty(), "  with a note that T3000 leaves the Value cell as it was");

        t = output_text(digital_point(23, 1), 0, kUnknownPanel);
        check(t.range == "custom range 1" && t.value == "1", "a custom range whose names were not read");
        check(!t.note.empty(), "  says so");

        OutputPanel named = panel_of(kCm5);
        named.ranges.digital_known       = true;
        named.ranges.digital[2].text       = "Shut/Run";
        named.ranges.digital[2].has_states = true;
        named.ranges.digital[2].off        = "Shut";
        named.ranges.digital[2].on         = "Run";
        named.ranges.digital[3].text       = "A/B/C";
        t = output_text(digital_point(25, 1), 0, named);
        check(t.range == "Shut/Run" && t.value == "Run", "custom range 3, with its names: Run");
        check(t.note.empty(), "  exactly as T3000");
        t = output_text(digital_point(26, 0), 0, named);
        check(t.range == "A/B/C" && t.value.empty(), "names that do not split in two: no value");
        check(!t.note.empty(), "  with a note");
    }

    void test_neither_analog_nor_digital()
    {
        section("digital_analog neither 0 nor 1");

        OutputPoint p = analog_point(1, 5000);
        p.digital_analog = 2;
        const OutputText t = output_text(p, 0, kUnknownPanel);
        check(t.range.empty() && t.units.empty() && t.value.empty(), "no range, units or value");
        check(!t.note.empty(), "and a note that T3000 leaves them as they were");
    }

    void test_the_hoa_switch()
    {
        section("the HOA Switch column, and what it does to Auto/Man");

        OutputPoint p = analog_point(1);
        p.auto_manual = 1;

        OutputText t = output_text(p, 0, kUnknownPanel);
        check(t.hoa.empty() && t.auto_manual == "Manual" && !t.hand, "settings not read: no switch column");

        t = output_text(p, 0, panel_of(kCm5));
        check(t.hoa.empty() && t.auto_manual == "Manual", "a CM5: no switch column");

        p.hw_switch_status = 0;
        t = output_text(p, 0, panel_of(kBigMiniPanel));
        check(t.hoa == "MAN-OFF" && t.hand, "a switched output at off: MAN-OFF, and a red row");
        check(t.auto_manual.empty(), "  Auto/Man is not written");
        check(t.note.find("Manual") != std::string::npos, "  and the note keeps the setting");

        p.hw_switch_status = 2;
        t = output_text(p, 23, panel_of(kBigMiniPanel));
        check(t.hoa == "MAN-ON" && t.hand, "output 24 - the last switched one - at hand: MAN-ON");

        t = output_text(p, 24, panel_of(kBigMiniPanel));
        check(t.hoa == "AUTO" && !t.hand && t.auto_manual == "Manual",
              "output 25 is past the switches: AUTO, whatever the switch byte says");

        p.hw_switch_status = 1;
        t = output_text(p, 0, panel_of(kBigMiniPanel));
        check(t.hoa == "AUTO" && !t.hand && t.auto_manual == "Manual", "at auto: AUTO, and Auto/Man written");
        check(t.note.empty(), "  exactly as T3000");

        p.hw_switch_status = 3;
        check(output_text(p, 0, panel_of(kBigMiniPanel)).hoa == "AUTO", "3 is shown as AUTO");
        p.hw_switch_status = 255;
        check(output_text(p, 0, panel_of(kBigMiniPanel)).hoa == "AUTO", "and so is 255");

        p.hw_switch_status = 0;
        t = output_text(p, 0, panel_of(kOem));
        check(t.hoa == "AUTO" && !t.hand, "a T3-OEM has the column but no switched outputs: AUTO");

        t = output_text(p, 13, panel_of(kT38AI8AO6DO));
        check(t.hoa == "MAN-OFF", "a T3-8AI8AO6DO switches its first 14");
        t = output_text(p, 14, panel_of(kT38AI8AO6DO));
        check(t.hoa == "AUTO", "  and not the 15th");
    }

    void test_an_external_output()
    {
        section("an output on a sub-device");

        OutputPoint p = analog_point(1);
        p.sub_id           = 3;
        p.sub_product      = 44;   // PM_T38AI8AO6DO
        p.sub_number       = 0;
        p.hw_switch_status = 2;

        OutputText t = output_text(p, 40, panel_of(kCm5));
        check(t.external, "on a CM5, it is external");
        check(t.product_output == "DO1", "  its Product Output is DO1");
        check(t.hoa == "MAN-ON" && t.hand, "  and its own switch fills the HOA column, though a CM5 has none");
        check(t.auto_manual == "Auto", "  leaving Auto/Man as the setting says");
        check(!t.note.empty(), "  and the note says what T5000 does not show");

        p.sub_number = 0x82;
        check(output_text(p, 40, panel_of(kCm5)).product_output == "AO3", "bit 7 set: AO, numbered from 1");

        // After the switch column, and over it: output 25 of a big panel is
        // not switched, and an external one still shows its own switch.
        p.hw_switch_status = 0;
        t = output_text(p, 24, panel_of(kBigMiniPanel));
        check(t.hoa == "MAN-OFF" && t.hand, "external on a big panel, past its switches: MAN-OFF");

        p.hw_switch_status = 1;
        t = output_text(p, 24, panel_of(kBigMiniPanel));
        check(t.hoa == "AUTO" && !t.hand, "and at auto: AUTO, no red row");

        check(!output_text(p, 40, panel_of(kT38AI8AO6DO)).external, "not on a panel that shows none external");
        check(!output_text(p, 40, kUnknownPanel).external, "not when the settings were not read");

        OutputPoint q = p;
        q.sub_product = 9;   // a Tstat8: not one of the expansion modules
        check(!output_text(q, 40, panel_of(kCm5)).external, "not for a product outside T3000's list");
        q = p;
        q.sub_id = 0;
        check(!output_text(q, 40, panel_of(kCm5)).external, "not without a sub_id");
        q = p;
        q.sub_product = 0;
        check(!output_text(q, 40, panel_of(kCm5)).external, "not without a sub_product");
    }
}

int run_output_text_tests()
{
    test_the_plain_columns();
    test_an_analog_output();
    test_a_digital_output();
    test_neither_analog_nor_digital();
    test_the_hoa_switch();
    test_an_external_output();
    return 0;
}
