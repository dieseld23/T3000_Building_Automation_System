#include "output_text.h"

#include <stdio.h>

#include "device_text.h"
#include "input_text.h"
#include "tables.h"

namespace t5000::display
{
    namespace
    {
        // As in input_text.cpp: ranges 1-22 are the fixed digital ranges,
        // 23-30 the device's own.
        constexpr int kLastFixedDigitalRange   = 22;
        constexpr int kFirstCustomDigitalRange = 23;
        constexpr int kLastCustomDigitalRange  = 30;
        static_assert(count(kDigitalUnits) == kLastFixedDigitalRange + 1,
                      "T3000 bounds digital ranges by the literal 22");

        // Anything but off or hand - HW_SW_AUTO, or a value no switch has -
        // is shown as AUTO.
        using device::kSwitchHand;
        using device::kSwitchOff;

        void add_note(std::string& note, const std::string& more)
        {
            if (!note.empty())
                note += ' ';
            note += more;
        }

        bool split_states(const std::string& pair, std::string& off, std::string& on)
        {
            const std::vector<std::wstring> parts = split_like_t3000(utf8_to_wide(pair), L'/');
            if (parts.size() != 2)
                return false;
            off = wide_to_utf8(parts[0]);
            on  = wide_to_utf8(parts[1]);
            return true;
        }

        std::string trimmed(const uint8_t* text, size_t length)
        {
            return wide_to_utf8(trim_like_t3000(acp_to_wide(text, length)));
        }

        // "%.1f" of tenths, or nothing for 0 (BacnetOutput.cpp:721-732).
        std::string volts(uint8_t tenths)
        {
            if (tenths == 0)
                return std::string();
            char buf[16];
            snprintf(buf, sizeof(buf), "%.1f", tenths / 10.0);
            return buf;
        }

        const char* auto_manual(uint8_t a)
        {
            return a == 0 ? "Auto" : "Manual";
        }

        // Analog: :866-904.
        void analog(const wire::OutputPoint& p, OutputText& t)
        {
            const int range = p.range;
            if (range == 0)
                t.range = "Unused";
            else if ((size_t)range < count(kOutputAnalogRanges))
                t.range = kOutputAnalogRanges[range];
            else
                t.range = "Out of range";

            if ((size_t)range < count(kOutputAnalogUnits))
                t.units = kOutputAnalogUnits[range];
            else
                t.units = "Unused";

            t.value = thousandths(p.value);
        }

        // Digital: :906-968.
        void digital(const wire::OutputPoint& p, const OutputPanel& panel, OutputText& t)
        {
            const int range = p.range;
            const bool custom = range >= kFirstCustomDigitalRange && range <= kLastCustomDigitalRange;

            if (range == 0)
            {
                t.value = thousandths(p.value);
                t.range = kDigitalUnits[0];
            }
            else if (range <= kLastFixedDigitalRange)
            {
                t.range = kDigitalUnits[range];
                std::string off, on;
                if (split_states(kDigitalUnits[range], off, on))
                    t.value = p.control == 0 ? off : on;
            }
            else if (custom && panel.ranges.digital_known)
            {
                const DigitalRange& r = panel.ranges.digital[range - kFirstCustomDigitalRange];
                t.range = r.text;
                if (r.has_states)
                {
                    t.value = p.control == 0 ? r.off : r.on;
                }
                else
                {
                    add_note(t.note, "The device's names for custom digital range " +
                                         std::to_string(range - kFirstCustomDigitalRange + 1) +
                                         " do not split into two states, so T3000 shows no value; its "
                                         "Value cell keeps the text it had.");
                }
            }
            else if (custom)
            {
                // T3000 shows "Unused" here until it has the names, and a
                // Value split from whatever the previous row left in temp1
                // (:925-928, :951-952).
                const int which = range - kFirstCustomDigitalRange + 1;
                t.range = "custom range " + std::to_string(which);
                t.value = p.control == 0 ? "0" : "1";
                add_note(t.note, "Range " + std::to_string(range) + " is the device's custom digital range " +
                                     std::to_string(which) + ". Its state names are stored on the device, and "
                                     "the device did not send them, so the state is shown as 0 or 1.");
            }
            else
            {
                t.range = kDigitalUnits[0];
                add_note(t.note, "T3000 shows no value for a digital output on range " + std::to_string(range) +
                                     "; its Value cell keeps the text it had.");
            }
        }

        // The HOA Switch column, and what it does to Auto/Man (:733-858).
        void switches(const wire::OutputPoint& p, int index, const OutputPanel& panel, OutputText& t)
        {
            t.auto_manual = auto_manual(p.auto_manual);
            if (!panel.known || !device::shows_hoa_switch(panel.type))
                return;

            t.hoa = "AUTO";
            const int switched = panel.rows.special.digital + panel.rows.special.analog;
            if (index >= switched)
                return;

            if (p.hw_switch_status == kSwitchOff || p.hw_switch_status == kSwitchHand)
            {
                t.hoa  = p.hw_switch_status == kSwitchOff ? "MAN-OFF" : "MAN-ON";
                t.hand = true;
                // T3000 greys Auto/Man out and does not write it (:759-771),
                // so it keeps whatever it held: nothing, on a first open.
                t.auto_manual.clear();
                add_note(t.note, std::string("The hand-off-auto switch is at ") + t.hoa +
                                     ", which overrides the software setting, so T3000 leaves Auto/Man "
                                     "as it was - empty when the grid is first shown. The setting is " +
                                     auto_manual(p.auto_manual) + ".");
            }
        }

        // An output on a sub-device (:1000-1072).
        void external(const wire::OutputPoint& p, const OutputPanel& panel, OutputText& t)
        {
            if (p.sub_id == 0 || p.sub_product == 0 || !panel.known || !panel.rows.shows_external ||
                !device::is_external_output_product(p.sub_product))
                return;

            t.external = true;

            // Bit 7 of sub_number says analog; the rest is the output's
            // index on the sub-device (:1026-1038).
            const bool is_analog = (p.sub_number & 0x80) != 0;
            const int number = (p.sub_number & 0x7F) + 1;
            t.product_output = (is_analog ? "AO" : "DO") + std::to_string(number);

            // Whatever the panel type decided above, the sub-device's own
            // switch is what the column shows (:1041-1062).
            t.hand = p.hw_switch_status == kSwitchOff || p.hw_switch_status == kSwitchHand;
            t.hoa  = p.hw_switch_status == kSwitchOff    ? "MAN-OFF"
                     : p.hw_switch_status == kSwitchHand ? "MAN-ON"
                                                          : "AUTO";

            add_note(t.note, "An output of sub-device " + std::to_string(p.sub_id) + " (product " +
                                 std::to_string(p.sub_product) + "), its " + t.product_output +
                                 ". T3000 marks it External and names the product; T5000 does not "
                                 "show those columns yet.");
        }
    }

    OutputText output_text(const wire::OutputPoint& p, int index, const OutputPanel& panel)
    {
        OutputText t;

        // Full Label and Label: each trimmed (:701-704, :1131-1134).
        t.full_label = trimmed(p.description, wire::kOutputDescriptionLength);
        t.label      = trimmed(p.label, wire::kOutputLabelLength);

        t.low_voltage  = volts(p.low_voltage);
        t.high_voltage = volts(p.high_voltage);

        switches(p, index, panel, t);

        if (p.digital_analog == 1)
        {
            analog(p, t);
        }
        else if (p.digital_analog == 0)
        {
            digital(p, panel, t);
        }
        else
        {
            add_note(t.note, "digital_analog is " + std::to_string(p.digital_analog) +
                                 ", neither analog (1) nor digital (0); T3000 leaves this row's "
                                 "value, units and range as they were.");
        }

        external(p, panel, t);

        // Status (:1117-1123), and PWM Period (:1126).
        if (p.decom == 0)
            t.status = kOutputStatus[0];
        else if (p.decom == 1)
            t.status = kOutputStatus[1];

        char buf[16];
        snprintf(buf, sizeof(buf), "%u", (unsigned)p.pwm_period);
        t.pwm_period = buf;

        return t;
    }
}
