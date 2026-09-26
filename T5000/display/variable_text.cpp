#include "variable_text.h"

#include <stdio.h>

#include "device_text.h"
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
                      "T3000 bounds digital ranges by the literal 23");

        // An analog variable on range 20 is a time (:363).
        constexpr int kTimeRange = 20;

        // The device's own units, 34-38 (:398), and its multi-state ranges,
        // 101-104 (:317, :408).
        constexpr int kFirstCustomUnitRange = 34;
        constexpr int kLastCustomUnitRange  = 38;
        static_assert(kLastCustomUnitRange - kFirstCustomUnitRange + 1 == wire::kVariableUnitCount,
                      "one range per custom unit");
        static_assert(count(kVariableUnits) == kFirstCustomUnitRange,
                      "range < 34 picks a fixed unit, so 34 is where the device's own begin");

        constexpr int kFirstMsvRange = 101;
        constexpr int kLastMsvRange  = 104;
        static_assert(kLastMsvRange - kFirstMsvRange + 1 == wire::kMsvTableCount, "one range per table");

        // What T3000 leaves in a cell it does not write, on the grid's first
        // showing: nothing. Said in every note about such a cell.
        const char* const kKeepsItsText = "keeps the text it had, which is nothing when the grid is first shown";

        void add_note(std::string& note, const std::string& more)
        {
            if (!note.empty())
                note += ' ';
            note += more;
        }

        std::string trimmed(const uint8_t* text, size_t length)
        {
            return wide_to_utf8(trim_like_t3000(acp_to_wide(text, length)));
        }

        bool is_msv_range(int range)
        {
            return range >= kFirstMsvRange && range <= kLastMsvRange;
        }

        // Ranges 101-104, digital or analog alike (:317-329, :408-420; the
        // copy of the branch at :421-433 can never be reached).
        void multi_state(const wire::VariablePoint& p, const VariablePanel& panel, VariableText& t)
        {
            const int table = p.range - kFirstMsvRange;
            const VariableRanges& names = panel.names;
            const MsvTable& m = names.msv[table];

            // The state's name, looked up by the whole part of value / 1000,
            // cut toward zero from a float: -500 looks up 0.
            const float v = ((float)p.value) / 1000;
            std::string name;
            t.value = msv_item_name(names, table, (int)v, name) ? name : variable_number(p.value);

            if (names.msv_known())
            {
                t.units = m.range;
                return;
            }

            // T3000 writes Units only once every table it asked for has come
            // back (:319, :410).
            const std::string which = std::to_string(table + 1);
            if (m.read)
                t.units = m.range;
            else if (names.msv_asked[table])
                t.units = "multi-state table " + which;
            add_note(t.note, std::string("Not every multi-state table the device was asked for came back, so "
                                         "T3000 writes no Units for a multi-state variable: the cell ") +
                                 kKeepsItsText + ".");
            if (names.msv_asked[table] && !m.read)
            {
                add_note(t.note, "Range " + std::to_string(p.range) + " names its states in table " + which +
                                     ", which did not come back, so the value is shown as a number, as "
                                     "T3000 shows it.");
            }
        }

        // Digital: :305-358.
        void digital(const wire::VariablePoint& p, const VariablePanel& panel, VariableText& t)
        {
            const int range = p.range;

            if (range == 0 || (range > kLastCustomDigitalRange && range < 100))
            {
                t.value = variable_number(p.value);
                t.units = kVariableUnits[0];
                return;
            }
            if (is_msv_range(range))
            {
                multi_state(p, panel, t);
                return;
            }

            // 1-30, 100 and 105 up: a state pair, split to choose the value.
            if (range <= kLastFixedDigitalRange)
            {
                const std::vector<std::wstring> parts = split_like_t3000(utf8_to_wide(kDigitalUnits[range]), L'/');
                t.units = kDigitalUnits[range];
                t.value = wide_to_utf8(parts.size() == 2 ? parts[p.control == 0 ? 0 : 1] : std::wstring());
                return;
            }

            if (range <= kLastCustomDigitalRange)
            {
                const int which = range - kFirstCustomDigitalRange + 1;
                if (!panel.ranges.digital_known)
                {
                    // T3000 splits an empty string here (:337-341): neither
                    // Value nor Units is written. Naming the range says more.
                    t.units = "custom range " + std::to_string(which);
                    t.value = p.control == 0 ? "0" : "1";
                    add_note(t.note, "Range " + std::to_string(range) + " is the device's custom digital range " +
                                         std::to_string(which) + ". Its state names are stored on the device, "
                                         "and the device did not send them, so the state is shown as 0 or 1. "
                                         "Until it has them, T3000 writes neither Value nor Units: each " +
                                         kKeepsItsText + ".");
                    return;
                }

                const DigitalRange& r = panel.ranges.digital[range - kFirstCustomDigitalRange];
                t.units = r.text;
                if (r.has_states)
                {
                    t.value = p.control == 0 ? r.off : r.on;
                }
                else
                {
                    add_note(t.note, "The device's names for custom digital range " + std::to_string(which) +
                                         " do not split into two states, so T3000 writes neither Value nor "
                                         "Units: each " + kKeepsItsText + ".");
                }
                return;
            }

            // 100, and 105 up: "Unused", which splits into one part, so no
            // value (:342-346).
            t.units = kVariableUnits[0];
            add_note(t.note, "T3000 shows no value for a digital variable on range " + std::to_string(range) +
                                 "; its Value cell " + kKeepsItsText + ".");
        }

        // Analog: :360-444.
        void analog(const wire::VariablePoint& p, const VariablePanel& panel, VariableText& t)
        {
            const int range = p.range;

            if (range == kTimeRange)
            {
                // Whole seconds by integer division, so -999 is 0 and shows
                // no sign (:370-371). T3000 then cuts the text to 21 and
                // trims it, which changes nothing it can produce.
                t.units = kVariableUnits[kTimeRange];
                t.value = interval_text(p.value / 1000);
            }
            else if ((size_t)range < count(kVariableUnits))
            {
                t.units = kVariableUnits[range];
                t.value = variable_number(p.value);
            }
            else if (range <= kLastCustomUnitRange)
            {
                const int unit = range - kFirstCustomUnitRange;
                t.value = variable_number(p.value);
                if (panel.names.unit_read[unit])
                {
                    t.units = panel.names.units[unit];
                }
                else
                {
                    t.units = "custom unit " + std::to_string(unit + 1);
                    add_note(t.note, "Range " + std::to_string(range) + " takes the device's custom unit " +
                                         std::to_string(unit + 1) + ", whose name is stored on the device, and "
                                         "the device did not send it. T3000 shows the name it last read, which "
                                         "is nothing until one has been read.");
                }
            }
            else if (is_msv_range(range))
            {
                multi_state(p, panel, t);
            }
            else
            {
                t.units = kVariableUnits[0];
                t.value = variable_number(p.value);
            }
        }
    }

    std::string variable_number(int32_t value)
    {
        const float v = ((float)value) / 1000;
        char buf[32];
        snprintf(buf, sizeof(buf), "%.3f", v);
        return buf;
    }

    std::string interval_text(long seconds)
    {
        std::string text;
        if (seconds < 0)
        {
            seconds = -seconds;
            text = "-";
        }

        const unsigned long hours   = (unsigned long)seconds / 3600;
        const unsigned long minutes = (unsigned long)(seconds % 3600) / 60;
        const unsigned long secs    = (unsigned long)(seconds % 3600) % 60;

        char buf[32];
        snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", hours, minutes, secs);
        return text + buf;
    }

    VariableText variable_text(const wire::VariablePoint& p, const VariablePanel& panel)
    {
        VariableText t;

        // Full Label and Label: each trimmed (:277-280, :457-460).
        t.full_label = trimmed(p.description, wire::kVariableDescriptionLength);
        t.label      = trimmed(p.label, wire::kVariableLabelLength);

        // :294-301.
        t.auto_manual = p.auto_manual == 0 ? "Auto" : "Manual";

        if (p.digital_analog == kVariableDigital)
            digital(p, panel, t);
        else
            analog(p, panel, t);

        return t;
    }
}
