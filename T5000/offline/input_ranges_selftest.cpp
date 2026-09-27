// Tests for the ranges an input configured offline can be given: T3000's
// Range dialog's numbers, the bytes each sets, the rows whose range is fixed,
// and which ranges each row is offered.
//
// That the captions and numbers are T3000's is checked against T3000's
// source by conformance/input_range_guard.cpp. These check that the rules
// built on them hold for every model T5000 lets be configured offline.

#include "input_ranges.h"

#include <algorithm>
#include <string>

#include "../testing/check.h"

namespace
{
    using namespace t5000::offline;
    using namespace t5000::testing;
    using t5000::device::MiniType;
    using t5000::device::ProductClassId;

    bool has(const std::vector<int>& numbers, int n)
    {
        return std::find(numbers.begin(), numbers.end(), n) != numbers.end();
    }

    std::string name_of(int number)
    {
        const InputRangeChoice* c = find_input_range(number);
        return c ? input_range_name(*c) : std::string("(none)");
    }

    std::vector<int> offered(ProductClassId product, MiniType type, int row)
    {
        return input_ranges_offered(product, type, row);
    }

    void test_the_choices()
    {
        section("the ranges offered are the Range dialog's, numbered as its box numbers them");

        const auto& choices = input_range_choices();
        if (!require(!choices.empty(), "there are ranges to choose"))
            return;

        check_eq(choices.front().number, 0, "the first is 0");
        check(name_of(0) == "Unused", "  Unused, as the Range column shows it");

        bool ascending = true;
        for (size_t i = 1; i < choices.size(); i++)
            ascending = ascending && choices[i - 1].number < choices[i].number;
        check(ascending, "each number once, in the dialog's order");

        bool digital = true;
        for (int n = 1; n <= 22; n++)
            digital = digital && find_input_range(n) != nullptr;
        check(digital, "every digital range, 1 to 22");
        check(name_of(1) == "Off/On", "  1 is Off/On");
        check(name_of(22) == "High/Low", "  22 is High/Low");

        bool custom = false;
        for (int n = 23; n <= 30; n++)
            custom = custom || find_input_range(n) != nullptr;
        check(!custom, "no custom digital range, 23 to 30: they are named from the panel");

        check(name_of(41) == "0.0 to 5.0 Volts", "41 is analog range 11, 0.0 to 5.0 Volts");
        check(name_of(33) == "10K Type2 \xC2\xB0" "C", "33, the first of a pair, is named with \xC2\xB0" "C");
        check(name_of(34) == "Deg.F 10K Type2", "  and 34, the second, as T3000's caption names it");
        check(name_of(50) == "Table 1" && name_of(54) == "Table 5", "50 to 54 are the five tables");
        check(name_of(66) == "AHKC-AC DC", "66 is the last named analog range");

        check(find_input_range(65) == nullptr, "65, a button with no name, is not offered");
        check(!find_input_range(67) && !find_input_range(68) && !find_input_range(69), "  nor 67 to 69");
        check(!find_input_range(101) && !find_input_range(103), "nor a multi-state range");
        check(!find_input_range(70) && !find_input_range(-1) && !find_input_range(255), "nor any other number");
        check_eq((long)choices.size(), 1 + 22 + 35, "Unused, 22 digital and 35 analog");
    }

    void test_the_bytes_a_range_sets()
    {
        section("a range sets the analog/digital byte and the range byte");

        auto is = [](int number, int digital_analog, int range) {
            const InputRangeBytes b = input_range_bytes(number);
            return b.digital_analog == digital_analog && b.range == range;
        };
        check(is(0, 1, 0), "0 is analog range 0, as T3000's OK makes it");
        check(is(1, 0, 1), "1 is digital range 1");
        check(is(22, 0, 22), "22 is digital range 22");
        check(is(31, 1, 1), "31 is analog range 1");
        check(is(66, 1, 36), "66 is analog range 36");

        check_eq(input_range_number(0, 0), 0, "digital range 0 opens the dialog at 0");
        check_eq(input_range_number(1, 0), 0, "  and so does analog range 0");
        check_eq(input_range_number(0, 5), 5, "digital range 5 at 5");
        check_eq(input_range_number(1, 11), 41, "analog range 11 at 41");

        bool round_trip = true;
        for (const auto& c : input_range_choices())
        {
            const InputRangeBytes b = input_range_bytes(c.number);
            round_trip = round_trip && input_range_number(b.digital_analog, b.range) == c.number;
        }
        check(round_trip, "every range opens the dialog at its own number");
    }

    void test_the_rows_whose_range_is_fixed()
    {
        section("the rows whose range T3000 does not let be changed");

        const auto fixed = [](ProductClassId p, MiniType t, int row) { return input_range_fixed(p, t, row); };

        check(!fixed(ProductClassId::Tstat10, MiniType::Oem, 12) && fixed(ProductClassId::Tstat10, MiniType::Oem, 13) &&
                  fixed(ProductClassId::Tstat10, MiniType::Oem, 17) && !fixed(ProductClassId::Tstat10, MiniType::Oem, 18),
              "T3-OEM: rows 13 to 17, inputs 14 to 18");
        check(!fixed(ProductClassId::Tstat10, MiniType::Oem12I, 16) && fixed(ProductClassId::Tstat10, MiniType::Oem12I, 17) &&
                  fixed(ProductClassId::Tstat10, MiniType::Oem12I, 21) && !fixed(ProductClassId::Tstat10, MiniType::Oem12I, 22),
              "T3-OEM-12I: rows 17 to 21");
        check(!fixed(ProductClassId::Tstat10, MiniType::Tstat10, 8) && fixed(ProductClassId::Tstat10, MiniType::Tstat10, 9) &&
                  fixed(ProductClassId::Tstat10, MiniType::Tstat10, 12) && !fixed(ProductClassId::Tstat10, MiniType::Tstat10, 13),
              "TSTAT10: rows 9 to 12");
        check(fixed(ProductClassId::Tstat10, MiniType::Tstat11, 9), "a TSTAT11 on TSTAT10 hardware: the same");
        check(!fixed(ProductClassId::Esp32T3Series, MiniType::Tstat11, 9),
              "  but not a TSTAT11 on ESP32 hardware, as T3000's product list pairs it: T3000 tests the product first");

        const ProductClassId esp = ProductClassId::Esp32T3Series;
        check(!fixed(esp, MiniType::Rmc1232, 7) && fixed(esp, MiniType::Rmc1232, 8) && fixed(esp, MiniType::Rmc1232, 11) &&
                  !fixed(esp, MiniType::Rmc1232, 12),
              "T3-RMC-1232: rows 8 to 11");
        check(!fixed(esp, MiniType::Rmc1232, 31) && fixed(esp, MiniType::Rmc1232, 32) && fixed(esp, MiniType::Rmc1232, 47) &&
                  !fixed(esp, MiniType::Rmc1232, 48),
              "  and 32 to 47");
        check(!fixed(esp, MiniType::Bms, 31) && fixed(esp, MiniType::Bms, 32) && fixed(esp, MiniType::Bms, 47) &&
                  !fixed(esp, MiniType::Bms, 48) && !fixed(esp, MiniType::Bms, 8),
              "T3-BMS: rows 32 to 47 only");
        check(!fixed(esp, MiniType::EspRmc, 15) && fixed(esp, MiniType::EspRmc, 16) && fixed(esp, MiniType::EspRmc, 17) &&
                  !fixed(esp, MiniType::EspRmc, 18),
              "T3-RMC: rows 16 and 17");
        check(!fixed(esp, MiniType::Ng3, 23) && fixed(esp, MiniType::Ng3, 24) && fixed(esp, MiniType::Ng3, 29) &&
                  !fixed(esp, MiniType::Ng3, 30),
              "T3-NG2: rows 24 to 29");
        check(fixed(ProductClassId::TstatAq, MiniType::NotSet, 0) && fixed(ProductClassId::AirlabEsp32, MiniType::NotSet, 40),
              "every row of a Tstat AQ or an Airlab ESP32");
        check(!fixed(ProductClassId::MiniPanelArm, MiniType::MiniPanelArm, 0) &&
                  !fixed(ProductClassId::Cm5, MiniType::Cm5, 0) && !fixed(esp, MiniType::EspLw, 30),
              "no row of a T3-BB, a CM5 or a T3-ESP-LW");

        // The same rows as device/product.h names for the TSTAT10's variants.
        const MiniType variants[] = { MiniType::Oem, MiniType::Oem12I, MiniType::Tstat10, MiniType::Tstat11 };
        for (const MiniType t : variants)
        {
            const auto info = t5000::device::mini_type_info(t);
            bool same = true;
            for (int row = 0; row < 64; row++)
                same = same && fixed(ProductClassId::Tstat10, t, row) ==
                                   (row >= info.fixed_range_first && row <= info.fixed_range_last);
            check(same, (std::string("  ") + t5000::device::to_string(t) + "'s rows agree with mini_type_info").c_str());
        }
    }

    void test_the_ranges_each_row_is_offered()
    {
        section("the ranges each row is offered, as the dialog enables its buttons");

        const std::vector<int> cm5 = offered(ProductClassId::Cm5, MiniType::Cm5, 0);
        check(has(cm5, 0) && has(cm5, 1) && has(cm5, 22) && has(cm5, 41) && has(cm5, 66), "a CM5's input 1: Unused, digital and analog");
        check(has(cm5, 45), "  the slow pulse count");
        check(!has(cm5, 55) && !has(cm5, 59), "  but neither the fast pulse count nor RPM");
        check_eq((long)cm5.size(), (long)input_range_choices().size() - 4, "  nor PT 1K: all but four");

        const ProductClassId arm = ProductClassId::MiniPanelArm;
        const std::vector<int> bb26 = offered(arm, MiniType::MiniPanelArm, 26);
        check(has(bb26, 55) && has(bb26, 59) && !has(bb26, 45),
              "a T3-BB's input 27: the fast pulse count and RPM, and not the slow");
        check(has(offered(arm, MiniType::MiniPanelArm, 31), 55), "  and its input 32");
        check(!has(offered(arm, MiniType::MiniPanelArm, 25), 55) && has(offered(arm, MiniType::MiniPanelArm, 25), 45),
              "  but not its input 26");
        check(!has(offered(arm, MiniType::MiniPanelArm, 32), 55), "  nor 33");

        check(has(offered(arm, MiniType::MiniPanelArmLb, 10), 55) && has(offered(arm, MiniType::MiniPanelArmLb, 16), 55) &&
                  !has(offered(arm, MiniType::MiniPanelArmLb, 9), 55) && !has(offered(arm, MiniType::MiniPanelArmLb, 17), 55),
              "a T3-LB: inputs 11 to 17 count fast pulses");
        const std::vector<int> tb0 = offered(arm, MiniType::MiniPanelArmTb, 0);
        check(has(tb0, 45) && !has(tb0, 55) && !has(tb0, 59), "a T3-TB's input 1: the slow pulse count, and neither the fast one nor RPM");
        check(has(offered(arm, MiniType::TinyMiniPanel, 5), 55) && !has(offered(arm, MiniType::TinyMiniPanel, 4), 55),
              "a Tiny MiniPanel: from input 6");
        check(has(offered(arm, MiniType::TinyExMiniPanel, 0), 55) && !has(offered(arm, MiniType::TinyExMiniPanel, 8), 55),
              "a Tiny EX MiniPanel: inputs 1 to 8");
        check(has(offered(arm, MiniType::BigMiniPanel, 26), 55) && has(offered(arm, MiniType::SmallMiniPanel, 10), 55),
              "the Big and Small MiniPanels as the T3-BB and T3-LB");
        const std::vector<int> tb11 = offered(arm, MiniType::Tb11I, 10);
        check(has(tb11, 45) && !has(tb11, 55) && !has(tb11, 59), "a T3-TB-11I's input 11: the slow pulse count only");

        const std::vector<int> fan4 = offered(arm, MiniType::FanModule, 4);
        check(has(fan4, 45) && has(fan4, 55) && has(fan4, 59), "a T3-FAN-MODULE's input 5: all three");
        const std::vector<int> fan3 = offered(arm, MiniType::FanModule, 3);
        check(has(fan3, 45) && !has(fan3, 55) && !has(fan3, 59), "  and its input 4: the slow pulse count only");

        const ProductClassId t10 = ProductClassId::Tstat10;
        check(offered(t10, MiniType::Oem, 8) == std::vector<int>({ 55, 59 }), "a T3-OEM's input 9: the fast pulse count and RPM, and nothing else");
        check(offered(t10, MiniType::Oem, 11) == std::vector<int>({ 55, 59 }), "  its input 12 the same");
        check(offered(t10, MiniType::Oem, 12) == std::vector<int>({ 33, 34 }), "  its input 13 a 10K Type2 sensor only");
        const std::vector<int> oem0 = offered(t10, MiniType::Oem, 0);
        check(has(oem0, 45) && has(oem0, 55) && has(oem0, 59),
              "  and its input 1 all three: no branch names the row, so the dialog leaves them as its resource has them");
        check(offered(t10, MiniType::Oem, 13).empty(), "  and its input 14 none: its range is fixed");

        check(offered(t10, MiniType::Oem12I, 12) == std::vector<int>({ 55, 59 }) &&
                  offered(t10, MiniType::Oem12I, 15) == std::vector<int>({ 55, 59 }),
              "a T3-OEM-12I's inputs 13 to 16: the fast pulse count and RPM");
        check(offered(t10, MiniType::Oem12I, 16) == std::vector<int>({ 33, 34 }), "  its input 17 a 10K Type2 sensor");
        check(offered(t10, MiniType::Oem12I, 17).empty(), "  its input 18 none");
        check(offered(t10, MiniType::Tstat10, 9).empty() && !offered(t10, MiniType::Tstat10, 8).empty(),
              "a TSTAT10's input 10 none, and its input 9 some");

        // Every model T5000 lets be chosen, every row: PT 1K is never
        // offered, and only ranges the dialog has.
        bool no_pt1k = true;
        bool known   = true;
        const auto models = t5000::device::known_models();
        for (int m = 0; m < models.count; m++)
        {
            for (int row = 0; row < 64; row++)
            {
                const std::vector<int> ranges = offered(models.entries[m].product, models.entries[m].type, row);
                no_pt1k = no_pt1k && !has(ranges, 39) && !has(ranges, 40);
                for (const int n : ranges)
                    known = known && find_input_range(n) != nullptr;
            }
        }
        check(no_pt1k, "no row of any model is offered PT 1K: its settings have not been read");
        check(known, "  and every range offered is one of the dialog's");
    }
}

int run_input_ranges_tests()
{
    test_the_choices();
    test_the_bytes_a_range_sets();
    test_the_rows_whose_range_is_fixed();
    test_the_ranges_each_row_is_offered();
    return 0;
}
