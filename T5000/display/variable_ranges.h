#pragma once

// The names a panel stores for the ranges only variables use, cut out of the
// replies the way T3000 cuts them.
//
//   Analog ranges 34-38 take their units from READVARUNIT_T3000: five
//   20-byte names (global_function.cpp:4371-4403), Analog_Variable_Units.
//   Ranges 101-104 are multi-state: range 101 + n names its values from
//   table n of READ_MSV_COMMAND's four (:4709-4736), each eight items of a
//   status, a name and the value it names.
//
// As in custom_ranges.h, these work on the reply's bytes: a multi-state
// item's name has no terminator of its own, and T3000's strlen runs on
// through whatever follows it.

#include <stddef.h>
#include <stdint.h>

#include <string>

#include "../wire/panel.h"

namespace t5000::display
{
    struct MsvItem
    {
        // status == 1 (Get_Msv_Item_Name, global_function.cpp:16861). Any
        // other status is an item T3000 skips.
        bool        enabled = false;
        std::string name;    // as T3000 takes it: untrimmed
        uint16_t    value = 0;
    };

    struct MsvTable
    {
        bool    read = false;   // the request that asks for it came back
        MsvItem items[wire::kMsvItemCount];

        // Custom_Msv_Range: what the Units column shows for the table's
        // range. Empty until read, as T3000 empties it before each read
        // (BacnetView.cpp:6479-6482).
        std::string range;
    };

    struct VariableRanges
    {
        // Analog_Variable_Units, per unit: T3000 keeps no flag, and shows
        // whatever it last read - nothing, until something is.
        bool        unit_read[wire::kVariableUnitCount] = {};
        std::string units[wire::kVariableUnitCount];

        // Which tables were asked for: 0-2 on firmware 60.7 and older, 0-3
        // on newer (BacnetView.cpp:6483-6547).
        bool     msv_asked[wire::kMsvTableCount] = {};
        MsvTable msv[wire::kMsvTableCount];

        // T3000's read_msv_table: every table asked for came back. Until
        // then T3000 writes no Units for a multi-state variable.
        bool msv_known() const;
    };

    // Fills `units` from a READVARUNIT_T3000 reply's entities: `count`
    // names of 20 bytes, the first of which is unit `first`. Returns false,
    // changing nothing, if the length is not count * 20 or the range runs
    // past unit 4.
    bool take_variable_units(const uint8_t* entities, size_t length, int first, int count,
                             VariableRanges& out);

    // Fills `msv` from a READ_MSV_COMMAND reply's entities: `count` tables
    // of 184 bytes, the first of which is table `first`. The same, for a
    // length or range that does not fit.
    bool take_msv_tables(const uint8_t* entities, size_t length, int first, int count,
                         VariableRanges& out);

    // Get_Msv_Table_Name (global_function.cpp:17025-17057), from a 184-byte
    // table: the first three enabled names that are not blank once trimmed,
    // joined by " / ", and " /..." after the third unless it was the last
    // item.
    std::string msv_range_text(const uint8_t* table);

    // Get_Msv_Item_Name (:16855-16871): the name of the first enabled item
    // in `table` (0-3) whose value is n. False when T3000 finds none, which
    // includes every lookup in table 3 - see variable_ranges.cpp.
    bool msv_item_name(const VariableRanges& ranges, int table, int n, std::string& name);
}
