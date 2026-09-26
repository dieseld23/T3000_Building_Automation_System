// Tests for what a panel's settings decide about its Outputs grid, against
// the chain at BacnetOutput.cpp:418-545, the HOA list at :733-755, and the
// ESP32 sizing at BacnetView.cpp:4963-4969.

#include "output_rows.h"
#include "../testing/check.h"

namespace
{
    using namespace t5000::device;
    using namespace t5000::testing;
    using t5000::wire::PanelSettings;

    PanelSettings panel(uint8_t mini_type_byte, int firmware = 600, uint8_t max_out = 0)
    {
        PanelSettings s;
        s.mini_type_byte = mini_type_byte;
        s.firmware_main  = (uint8_t)(firmware / 10);
        s.firmware_sub   = (uint8_t)(firmware % 10);
        s.max_out        = max_out;
        return s;
    }

    OutputRows chain(int device_type)
    {
        return output_rows_for(device_type, ProductClassId::Cm5, panel(0));
    }

    // rows, digital, analog, shows_external - all four, so a branch that
    // gets one of them wrong fails by name.
    bool is(const OutputRows& r, int rows, int digital, int analog, bool external)
    {
        return r.rows == rows && r.special.digital == digital && r.special.analog == analog &&
               r.shows_external == external;
    }

    void test_the_whole_chain()
    {
        section("the Outputs chain, every branch, by bacnet_device_type");

        check(is(chain(1), 64, 12, 12, true), "BIG_MINIPANEL: 64 rows, 12 digital and 12 analog switched");
        check(is(chain(5), 64, 12, 12, true), "MINIPANELARM: the same");
        check(is(chain(2), 64, 6, 4, true), "SMALL_MINIPANEL: 6 and 4");
        check(is(chain(6), 64, 6, 4, true), "MINIPANELARM_LB: the same");
        check(is(chain(3), 64, 6, 2, true), "TINY_MINIPANEL: 6 and 2");
        check(is(chain(4), 64, 8, 6, true), "TINY_EX_MINIPANEL: 8 and 6");
        check(is(chain(7), 64, 8, 6, true), "MINIPANELARM_TB: the same");
        check(is(chain(21), 64, 0, 6, true), "T3_ESP_LW: 0 and 6");
        check(is(chain(12), 64, 6, 5, true), "T3_TB_11I: 6 and 5");
        check(is(chain(44), 14, 6, 8, false), "PM_T38AI8AO6DO: 14 rows, none external");
        check(is(chain(43), 0, 0, 0, false), "PM_T322AI: no rows at all");
        check(is(chain(53), 0, 0, 0, false), "PM_T332AI_ARM: no rows at all");
        check(is(chain(104), 6, 0, 6, false), "PWM_TRANSDUCER: 6 rows, all analog");
        for (int stm32 = 210; stm32 <= 215; stm32++)
            check(is(chain(stm32), 3, 0, 0, false), "an STM32 sensor, 210-215: 3 rows");
        check(is(chain(216), 64, 0, 0, true), "216 is not in the STM32 list");
        check(is(chain(72), 8, 0, 0, true), "PM_T3_LC: 8 rows, and it can show external outputs");
        check(is(chain(95), 2, 0, 0, true), "PID_T36CTA: 2 rows");
        check(is(chain(19), 64, 7, 0, true), "T3_ESP_RMC: 7 digital switched, 64 rows");
        check(is(chain(29), 64, 4, 0, true), "T3_RMC1232: 4 digital");
        check(is(chain(10), 64, 0, 0, true), "T3_BMS: none switched");
        check(is(chain(22), 64, 8, 4, true), "T3_NG3: 8 and 4");
        check(is(chain(0), 64, 0, 0, true), "anything else: 64 rows, none switched");
        check(is(chain(11), 64, 0, 0, true), "T3_OEM is not in the chain");
    }

    void test_from_the_settings()
    {
        section("from the settings: the low six bits of mini_type");

        const OutputRows big = output_rows(ProductClassId::MiniPanelArm, panel(0x80 | 1));
        check(is(big, 64, 12, 12, true), "0x81 - an APM chip on a BIG_MINIPANEL - is a BIG_MINIPANEL");

        const OutputRows none = output_rows(ProductClassId::Esp32T3Series, panel(43));
        check(is(none, 0, 0, 0, false), "a T3-22AI on firmware before 63.7: no rows");
    }

    void test_esp32_sizing()
    {
        section("an ESP32 T3 on firmware 63.7 or later sizes its outputs from its settings");

        const ProductClassId esp32 = ProductClassId::Esp32T3Series;

        check_eq(outputs_to_read(esp32, panel(1, 637, 96)), 96, "63.7 with max_out 96: 96 read");
        check_eq(outputs_to_read(esp32, panel(1, 636, 96)), 64, "63.6: 64, whatever max_out says");
        check_eq(outputs_to_read(esp32, panel(1, 637, 64)), 64, "max_out 64: 64");
        check_eq(outputs_to_read(esp32, panel(1, 637, 63)), 64, "max_out 63: 64, not 63");
        check_eq(outputs_to_read(esp32, panel(1, 637, 65)), 65, "max_out 65: 65");
        check_eq(outputs_to_read(esp32, panel(1, 637, 20)), 64, "max_out below 64: still 64, as T3000 on a first open");
        check_eq(outputs_to_read(esp32, panel(1, 637, 255)), 255, "max_out 255: 255");
        check_eq(outputs_to_read(ProductClassId::MiniPanelArm, panel(1, 700, 96)), 64,
                 "not an ESP32 T3: 64, whatever max_out says");

        // The override comes after the chain and replaces only its row count.
        const OutputRows t38 = output_rows(esp32, panel(44, 637, 0));
        check(is(t38, 64, 6, 8, false), "a T3-8AI8AO6DO on 63.7: 64 rows, its switches kept, none external");

        const OutputRows t22 = output_rows(esp32, panel(43, 637, 96));
        check(is(t22, 96, 0, 0, false), "a T3-22AI on 63.7 with max_out 96: 96 rows, not 0");

        const OutputRows old = output_rows(esp32, panel(44, 636, 96));
        check(is(old, 14, 6, 8, false), "the same on 63.6: 14");
    }

    void test_the_hoa_list()
    {
        section("the panel types with a HOA Switch column");

        const int with[] = { 9, 27, 11, 14, 19, 29, 10, 22, 26, 1, 5, 6, 21, 12, 7, 2, 3, 4, 44, 43, 104 };
        bool all = true;
        for (const int t : with)
            all = all && shows_hoa_switch(t);
        check(all, "the 21 types T3000 lists");

        int others = 0;
        for (int t = 0; t < 256; t++)
            others += shows_hoa_switch(t) ? 1 : 0;
        check_eq(others, 21, "and no others");
        check(!shows_hoa_switch(0), "not CM5");
        check(!shows_hoa_switch(53), "not PID_T332AI");
        check(!shows_hoa_switch(8), "not MINIPANELARM_NB");
    }

    void test_the_external_products()
    {
        section("the products an output can be external to");

        // PM_T3PT10, PM_T3IOA, PM_T332AI, PM_T38AI16O, PM_T38I13O, PM_T34AO,
        // PM_T322AI, PM_T332AI_ARM, PM_T38AI8AO6DO, PM_T36CT, PM_T36CTA,
        // PM_T3_LC (BacnetOutput.cpp:1006-1017).
        const int listed[] = { 26, 21, 22, 23, 20, 28, 43, 53, 44, 29, 95, 72 };
        bool all = true;
        for (const int p : listed)
            all = all && is_external_output_product((uint8_t)p);
        check(all, "the twelve T3000 lists");

        int others = 0;
        for (int p = 0; p < 256; p++)
            others += is_external_output_product((uint8_t)p) ? 1 : 0;
        check_eq(others, 12, "and no others");
        check(!is_external_output_product(35), "not a MiniPanel");
        check(!is_external_output_product(88), "not an ESP32 T3");
    }
}

int run_output_rows_tests()
{
    test_the_whole_chain();
    test_from_the_settings();
    test_esp32_sizing();
    test_the_hoa_list();
    test_the_external_products();
    return 0;
}
