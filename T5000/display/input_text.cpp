#include "input_text.h"

#include <stdio.h>

#include "tables.h"

namespace t5000::display
{
    namespace
    {
        // T3000 names a digital range's states with the literal 22, not the
        // table's size (BacnetInput.cpp:1132, :1154), and treats 23-30 as the
        // device's custom ranges. The two agree only while the table has 23
        // entries; the guard test pins that count, and this pins the
        // assumption next to the code that relies on it.
        constexpr int kLastFixedDigitalRange = 22;
        static_assert(count(kDigitalUnits) == kLastFixedDigitalRange + 1,
                      "T3000 bounds digital ranges by the literal 22 - see above");

        constexpr int kFirstCustomDigitalRange = 23;
        constexpr int kLastCustomDigitalRange  = 30;

        constexpr int kFirstCustomAnalogRange = 20;
        constexpr int kLastCustomAnalogRange  = 24;

        // "Off/On" -> "Off", "On". T3000 shows a state only when the split
        // gives exactly two parts (BacnetInput.cpp:1162-1163).
        bool split_states(const char* pair, std::string& off, std::string& on)
        {
            const std::string s(pair);
            const size_t slash = s.find('/');
            if (slash == std::string::npos || s.find('/', slash + 1) != std::string::npos)
                return false;
            off = s.substr(0, slash);
            on  = s.substr(slash + 1);
            return true;
        }

        void add_note(std::string& note, const std::string& more)
        {
            if (!note.empty())
                note += ' ';
            note += more;
        }

        void analog(const wire::InputPoint& p, int index, const PanelContext& panel, InputText& t)
        {
            const int range = p.range;

            // Units, BacnetInput.cpp:1018-1029.
            if (range >= kFirstCustomAnalogRange && range <= kLastCustomAnalogRange)
            {
                // Analog_Custom_Units[range - 20]: the unit name the device
                // stores for one of its five custom tables.
                add_note(t.note, "Range " + std::to_string(range) + " is the device's custom table " +
                                     std::to_string(range - kFirstCustomAnalogRange + 1) +
                                     ". Its unit name is stored on the device, and T5000 "
                                     "does not read it yet.");
            }
            else if ((size_t)range < count(kInputAnalogUnits))
            {
                t.units = kInputAnalogUnits[range];
            }
            // Past the units table T3000 writes "" while the range is still
            // inside the range table, and nothing at all past that - where
            // the Range column says "Out of range". Empty either way here.

            // Range, :1041-1084.
            if (range == 0)
            {
                t.range = "Unused";
            }
            else if ((size_t)range < count(kInputAnalogRanges))
            {
                t.range = kInputAnalogRanges[range];

                // :1045-1060 give a T3-PT12 its own PT-100/PT-1000 names, and
                // :1078-1081 then overwrite them with the table entry for
                // every model except the RMC1232. T3000 never shows them, so
                // they are not ported. The RMC1232's own labels do survive.
                if (panel.known && panel.type == device::MiniType::Rmc1232)
                {
                    if (index == 8 || index == 9 || index == 10)
                        t.range = "-30V to -65V";
                    else if (index == 11)
                        t.range = "0 to 30V";
                }
            }
            else
            {
                t.range = "Out of range";
            }

            t.value = thousandths(p.value);

            // Calibration, :1096-1111: an unsigned 16-bit count of tenths,
            // with the sign in its own column.
            const unsigned short tenths = (unsigned short)(p.calibration_h * 256 + p.calibration_l);
            char buf[32];
            snprintf(buf, sizeof(buf), "%.1f", ((float)tenths) / 10);
            t.calibration = buf;
            t.sign        = p.calibration_sign == 0 ? "+" : "-";
        }

        void digital(const wire::InputPoint& p, InputText& t)
        {
            const int range = p.range;

            // :1117 clears Calibration for a digital point but never writes
            // Sign, which keeps the previous row's. Both are empty here.

            // Range, :1122-1142.
            if (range == 0)
            {
                t.value = thousandths(p.value);
                t.range = kDigitalUnits[0];
            }
            else if (range <= kLastFixedDigitalRange)
            {
                t.range = kDigitalUnits[range];
            }
            else if (range >= kFirstCustomDigitalRange && range <= kLastCustomDigitalRange)
            {
                // Custom_Digital_Range[range - 23], which T3000 fills from the
                // device. Until it has, T3000 shows "Unused" here, and the
                // Value cell keeps the previous row's text (:1156-1162).
                const int which = range - kFirstCustomDigitalRange + 1;
                t.range = "custom range " + std::to_string(which);
                add_note(t.note, "Range " + std::to_string(range) + " is the device's custom digital range " +
                                     std::to_string(which) + ". Its state names are stored on the device, and "
                                     "T5000 does not read them yet, so the state is shown as 0 or 1.");
            }
            else
            {
                t.range = kDigitalUnits[0];
            }

            // Value, :1145-1170. The state comes from `control`, not from
            // `value`: a digital input's value field is not its state.
            if (range >= 1 && range <= kLastFixedDigitalRange)
            {
                std::string off, on;
                if (split_states(kDigitalUnits[range], off, on))
                    t.value = p.control == 0 ? off : on;
            }
            else if (range >= kFirstCustomDigitalRange && range <= kLastCustomDigitalRange)
            {
                t.value = p.control == 0 ? "0" : "1";
            }
            else if (range > kLastCustomDigitalRange)
            {
                add_note(t.note, "T3000 shows no value for a digital input on range " + std::to_string(range) +
                                     "; its Value cell keeps the previous row's text.");
            }
        }
    }

    bool is_invalid_range(uint8_t range)
    {
        return range > 30 && range != 101 && range != 102 && range != 103 && range != 104;
    }

    std::string thousandths(int32_t value)
    {
        // ((float)value) / 1000, then "%.2f" (BacnetInput.cpp:1092-1093).
        const float v = ((float)value) / 1000;
        char buf[32];
        snprintf(buf, sizeof(buf), "%.2f", v);
        return buf;
    }

    InputText input_text(const wire::InputPoint& p, int index, const PanelContext& panel)
    {
        InputText t;

        t.auto_manual = p.auto_manual == 0 ? "Auto" : "Manual";

        if (p.digital_analog == 1)
        {
            analog(p, index, panel, t);
        }
        else if (p.digital_analog == 0)
        {
            digital(p, t);
        }
        else
        {
            // Neither branch at :1010 or :1113 is taken, so T3000 writes none
            // of the value, units, range or calibration cells.
            add_note(t.note, "digital_analog is " + std::to_string(p.digital_analog) +
                                 ", neither analog (1) nor digital (0); T3000 leaves this row's "
                                 "value, units and range as the previous row left them.");
        }

        char buf[16];
        snprintf(buf, sizeof(buf), "%d", (unsigned char)p.filter);
        t.filter = buf;

        // Status and Signal Type, :1177-1237: the two nibbles of `decom`.
        const int status = p.decom & 0x0F;
        const int jumper = (p.decom & 0xF0) >> 4;

        // No open/short alarm on an unused or unrecognised range.
        if (status == 0 || p.range == 0 || is_invalid_range(p.range))
        {
            t.status = kInputStatus[0];
        }
        else if (status == 1)
        {
            t.status = kInputStatus[1];
            t.alarm  = true;
        }
        else if (status == 2)
        {
            t.status = kInputStatus[2];
            t.alarm  = true;
        }

        // T3000 also clears the high nibble of its own copy when it is out
        // of range (:1213). T5000 shows the same text and leaves the point
        // as it was read.
        t.signal_type = (size_t)jumper < count(kJumperStatus) ? kJumperStatus[jumper] : kJumperStatus[0];

        return t;
    }
}
