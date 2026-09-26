// Tests for the names only variables use: the custom units, against the
// READVARUNIT_T3000 handler (global_function.cpp:4371-4403), and the
// multi-state tables, against the READ_MSV_COMMAND handler (:4709-4736),
// Get_Msv_Item_Name (:16855) and Get_Msv_Table_Name (:17025).

#include "variable_ranges.h"
#include "../testing/check.h"

#include <string.h>

#include <vector>

namespace
{
    using namespace t5000::display;
    using namespace t5000::testing;
    namespace w = t5000::wire;

    using Bytes = std::vector<uint8_t>;

    Bytes tables(int count)
    {
        return Bytes((size_t)count * w::kMsvTableWireSize, 0);
    }

    // Item k of table t: its status, its name (not terminated if it is 20
    // characters) and its value.
    void item(Bytes& b, int t, int k, uint8_t status, const char* name, uint16_t value)
    {
        uint8_t* at = &b[(size_t)t * w::kMsvTableWireSize + (size_t)k * w::kMsvItemWireSize];
        at[w::msv_item_at::status] = status;
        memset(at + w::msv_item_at::name, 0, w::msv_item_at::name_length);
        memcpy(at + w::msv_item_at::name, name, strlen(name));
        at[w::msv_item_at::value]     = (uint8_t)(value & 0xFF);
        at[w::msv_item_at::value + 1] = (uint8_t)(value >> 8);
    }

    Bytes units(const char* const (&names)[5])
    {
        Bytes b((size_t)w::kVariableUnitCount * w::kVariableUnitWireSize, 0);
        for (int i = 0; i < 5; i++)
            memcpy(&b[(size_t)i * w::kVariableUnitWireSize], names[i], strlen(names[i]));
        return b;
    }

    void test_custom_units()
    {
        section("the custom units: five names, untrimmed");

        const char* const names[5] = { "L/s", " kPa ", "", "gal/min", "m" };
        const Bytes b = units(names);
        VariableRanges r;
        check(take_variable_units(b.data(), b.size(), 0, 5, r), "a whole reply is taken");
        check(r.units[0] == "L/s" && r.units[3] == "gal/min" && r.units[4] == "m", "each name");
        check(r.units[1] == " kPa ", "  untrimmed, as T3000 shows it");
        check(r.units[2].empty() && r.unit_read[2], "an empty name is read, and empty");
    }

    void test_a_custom_unit_that_fills_its_field()
    {
        section("a custom unit of 20 characters is kept or blanked by the byte after it");

        const char* const names[5] = { "", "", "", "", "" };
        Bytes b = units(names);
        memset(&b[0], 'U', 20);    // unit 1, then unit 2 empty
        VariableRanges r;
        take_variable_units(b.data(), b.size(), 0, 5, r);
        check(r.units[0] == std::string(20, 'U'), "before an empty name: kept whole");

        b[20] = 'V';               // unit 2 starts
        take_variable_units(b.data(), b.size(), 0, 5, r);
        check(r.units[0].empty(), "before a name: blanked, as strlen runs on past 20");
        check(r.units[1] == "V", "  and the next is still read");

        memset(&b[80], 'Z', 20);   // the last, which nothing follows in the reply
        take_variable_units(b.data(), b.size(), 0, 5, r);
        check(r.units[4] == std::string(20, 'Z'), "the last name, at 20 characters: kept");
    }

    void test_custom_units_refuse_bad_input()
    {
        section("custom units that do not fit are refused, changing nothing");

        const char* const names[5] = { "a", "b", "c", "d", "e" };
        const Bytes b = units(names);
        VariableRanges r;
        check(!take_variable_units(b.data(), b.size() - 1, 0, 5, r), "a short reply");
        check(!take_variable_units(b.data(), b.size(), 1, 5, r), "a range past unit 4");
        check(!take_variable_units(nullptr, 0, 0, 1, r), "no reply");
        bool untouched = true;
        for (int i = 0; i < 5; i++)
            untouched = untouched && !r.unit_read[i] && r.units[i].empty();
        check(untouched, "  and nothing was taken");
    }

    void test_a_table_is_named_by_its_first_three_names()
    {
        section("a multi-state table's name: three names, and \" /...\" if another item follows");

        Bytes b = tables(1);
        item(b, 0, 0, 1, "Off", 0);
        item(b, 0, 1, 1, "Cool", 1);
        item(b, 0, 2, 1, "Heat", 2);
        check(msv_range_text(b.data()) == "Off / Cool / Heat /...",
              "three names at items 0-2: \" /...\", though no other item is enabled");

        b = tables(1);
        item(b, 0, 5, 1, "Off", 0);
        item(b, 0, 6, 1, "Cool", 1);
        item(b, 0, 7, 1, "Heat", 2);
        check(msv_range_text(b.data()) == "Off / Cool / Heat", "the third at item 7, the last: no \" /...\"");

        b = tables(1);
        item(b, 0, 0, 1, "  Low ", 0);
        item(b, 0, 1, 0, "Hidden", 1);
        item(b, 0, 2, 2, "Other", 2);
        item(b, 0, 3, 1, "   ", 3);
        item(b, 0, 4, 1, "High", 4);
        check(msv_range_text(b.data()) == "Low / High",
              "trimmed; a status other than 1 skipped; a blank name skipped and not counted");

        check(msv_range_text(tables(1).data()).empty(), "no enabled item: nothing");
    }

    void test_tables_are_taken()
    {
        section("multi-state tables are cut from a reply");

        Bytes b = tables(2);
        item(b, 0, 0, 1, "Off", 0);
        item(b, 1, 3, 1, "Stage 2", 258);
        VariableRanges r;
        check(take_msv_tables(b.data(), b.size(), 2, 2, r), "tables 2-3 are taken");
        check(r.msv[2].read && r.msv[3].read && !r.msv[0].read && !r.msv[1].read, "  as tables 2 and 3");
        check(r.msv[3].items[3].enabled && r.msv[3].items[3].value == 258, "a value is little-endian: 0x0102 is 258");
        check(r.msv[2].range == "Off", "and each table gets its name");

        check(!take_msv_tables(b.data(), b.size() - 1, 0, 2, r), "a short reply is refused");
        check(!take_msv_tables(b.data(), b.size(), 3, 2, r), "and a range past table 3");
    }

    void test_a_name_is_looked_up()
    {
        section("a multi-state name is looked up as Get_Msv_Item_Name looks it up");

        Bytes b = tables(4);
        item(b, 0, 0, 0, "Disabled", 5);
        item(b, 0, 1, 2, "Status 2", 5);
        item(b, 0, 2, 1, " First ", 5);
        item(b, 0, 3, 1, "Second", 5);
        item(b, 3, 0, 1, "In table 3", 1);
        VariableRanges r;
        take_msv_tables(b.data(), b.size(), 0, 4, r);

        std::string name;
        check(msv_item_name(r, 0, 5, name) && name == " First ",
              "the first item of status 1 with the value, untrimmed; status 0 and 2 are passed over");
        check(!msv_item_name(r, 0, 6, name), "no item with the value: none");
        check(!msv_item_name(r, 0, -1, name), "a negative value matches nothing");
        check(!msv_item_name(r, 3, 1, name), "table 3 is never looked in, though it was read and has a match");

        VariableRanges unread;
        check(!msv_item_name(unread, 0, 0, name), "a table that did not come back names nothing");
    }

    void test_a_name_with_no_end_runs_into_its_value()
    {
        section("an item name of 20 characters runs on into its value, as strlen does");

        Bytes b = tables(1);
        item(b, 0, 0, 1, "NNNNNNNNNNNNNNNNNNNN", 1);   // value 1: bytes 01 00
        VariableRanges r;
        take_msv_tables(b.data(), b.size(), 0, 1, r);
        std::string name;
        check(msv_item_name(r, 0, 1, name) && name == std::string(20, 'N') + "\x01",
              "20 N, then the value's low byte, then the high byte ends it");
    }

    void test_when_the_tables_are_known()
    {
        section("T3000 has its tables when every one it asked for came back");

        VariableRanges r;
        check(!r.msv_known(), "none asked for: not known");

        for (int t = 0; t < 3; t++)
            r.msv_asked[t] = true;
        Bytes b = tables(2);
        take_msv_tables(b.data(), b.size(), 0, 2, r);
        check(!r.msv_known(), "tables 0-1 of 0-2: not yet");

        b = tables(1);
        take_msv_tables(b.data(), b.size(), 2, 1, r);
        check(r.msv_known(), "0-2 of 0-2: known, and table 3 was never asked for");

        r.msv_asked[3] = true;
        check(!r.msv_known(), "0-2 of 0-3: not known");
    }
}

int run_variable_ranges_tests()
{
    test_custom_units();
    test_a_custom_unit_that_fills_its_field();
    test_custom_units_refuse_bad_input();
    test_a_table_is_named_by_its_first_three_names();
    test_tables_are_taken();
    test_a_name_is_looked_up();
    test_a_name_with_no_end_runs_into_its_value();
    test_when_the_tables_are_known();
    return 0;
}
