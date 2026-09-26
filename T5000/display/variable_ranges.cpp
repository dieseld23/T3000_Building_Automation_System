#include "variable_ranges.h"

#include "device_text.h"

namespace t5000::display
{
    namespace
    {
        // Get_Msv_Item_Name looks only in tables 0-2: `if (ntable > 2)
        // return -1` (global_function.cpp:16857), though T3000 keeps and
        // reads four. So a variable on range 104 always shows its value as a
        // number, whatever table 3 names it.
        constexpr int kLastLookedUpTable = 2;

        // T3000's own copy of a table is its reply bytes, and an item's name
        // is read with strlen: through the item's value, and into the next
        // item, when the 20 name bytes have no NUL. Past the table, strlen
        // would read the next table of T3000's copy; this stops at the end,
        // which is where a name that long has lost any meaning anyway.
        std::wstring item_name_wide(const uint8_t* table, int item)
        {
            const size_t at = (size_t)item * wire::kMsvItemWireSize + wire::msv_item_at::name;
            return acp_to_wide(table + at, wire::kMsvTableWireSize - at);
        }
    }

    bool VariableRanges::msv_known() const
    {
        bool asked = false;
        for (int t = 0; t < wire::kMsvTableCount; t++)
        {
            if (msv_asked[t] && !msv[t].read)
                return false;
            asked = asked || msv_asked[t];
        }
        return asked;
    }

    bool take_variable_units(const uint8_t* entities, size_t length, int first, int count,
                             VariableRanges& out)
    {
        if (entities == nullptr || first < 0 || count <= 0 || first + count > wire::kVariableUnitCount ||
            length != (size_t)count * wire::kVariableUnitWireSize)
            return false;

        for (int i = 0; i < count; i++)
        {
            const uint8_t* name = entities + (size_t)i * wire::kVariableUnitWireSize;

            // :4389-4392. A name blanked when strlen over the reply passes
            // 20: its 20 bytes and the byte after them, the next name's
            // first, all non-NUL. The last name's next byte is past the
            // reply, so its 20 bytes decide alone.
            const size_t left = length - (size_t)i * wire::kVariableUnitWireSize;
            const size_t limit = left > wire::kVariableUnitWireSize ? wire::kVariableUnitWireSize + 1 : left;
            const bool too_long = length_to_nul(name, limit) > wire::kVariableUnitWireSize;

            // Then MultiByteToWideChar over its strlen (:4395-4398): within
            // the 20 bytes, since the next name of T3000's copy is still the
            // one before this reply - zero, when it is the first. Untrimmed.
            out.units[first + i]     = too_long ? std::string() : acp_to_utf8(name, wire::kVariableUnitWireSize);
            out.unit_read[first + i] = true;
        }
        return true;
    }

    std::string msv_range_text(const uint8_t* table)
    {
        std::wstring text;
        int shown = 0;
        for (int k = 0; k < wire::kMsvItemCount; k++)
        {
            // Checked before the item, not after the third name: " /..."
            // follows three names whenever there is another item, whether
            // or not it names anything.
            if (shown >= 3)
            {
                text += L" /...";
                break;
            }

            const uint8_t* item = table + (size_t)k * wire::kMsvItemWireSize;
            if (item[wire::msv_item_at::status] != 1)
                continue;

            const std::wstring name = trim_like_t3000(item_name_wide(table, k));
            if (name.empty())
                continue;
            if (shown != 0)
                text += L" / ";
            text += name;
            shown++;
        }
        return wide_to_utf8(text);
    }

    bool take_msv_tables(const uint8_t* entities, size_t length, int first, int count,
                         VariableRanges& out)
    {
        if (entities == nullptr || first < 0 || count <= 0 || first + count > wire::kMsvTableCount ||
            length != (size_t)count * wire::kMsvTableWireSize)
            return false;

        for (int t = 0; t < count; t++)
        {
            const uint8_t* table = entities + (size_t)t * wire::kMsvTableWireSize;
            MsvTable& m = out.msv[first + t];

            for (int k = 0; k < wire::kMsvItemCount; k++)
            {
                const uint8_t* item = table + (size_t)k * wire::kMsvItemWireSize;
                m.items[k].enabled = item[wire::msv_item_at::status] == 1;
                m.items[k].name    = wide_to_utf8(item_name_wide(table, k));
                m.items[k].value   = (uint16_t)(item[wire::msv_item_at::value] |
                                                (item[wire::msv_item_at::value + 1] << 8));
            }
            m.range = msv_range_text(table);
            m.read  = true;
        }
        return true;
    }

    bool msv_item_name(const VariableRanges& ranges, int table, int n, std::string& name)
    {
        // A table that did not come back is, in T3000's copy on a first
        // open, zeros: no item is enabled, and nothing is found.
        if (table < 0 || table > kLastLookedUpTable || !ranges.msv[table].read)
            return false;

        for (const MsvItem& item : ranges.msv[table].items)
        {
            // An int against an unsigned short: a negative n matches nothing.
            if (item.enabled && n == (int)item.value)
            {
                name = item.name;
                return true;
            }
        }
        return false;
    }
}
