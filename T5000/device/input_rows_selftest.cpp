// Tests for what a panel's settings decide about its Inputs grid, against
// BacnetInput.cpp:736-807 and the ESP32 sizing at BacnetView.cpp:4963-4966.

#include "input_rows.h"
#include "../testing/check.h"

namespace
{
    using namespace t5000::device;
    using namespace t5000::testing;
    using t5000::wire::PanelSettings;

    PanelSettings panel(uint8_t mini_type_byte, int firmware = 600, uint8_t max_in = 0)
    {
        PanelSettings s;
        s.mini_type_byte = mini_type_byte;
        s.firmware_main  = (uint8_t)(firmware / 10);
        s.firmware_sub   = (uint8_t)(firmware % 10);
        s.max_in         = max_in;
        return s;
    }

    int rows(uint8_t mini_type_byte, ProductClassId product = ProductClassId::Cm5)
    {
        const InputRows r = input_rows(product, panel(mini_type_byte));
        return r.set ? r.rows : -1;
    }

    int chain(int device_type)
    {
        const InputRows r = input_rows_for(device_type, ProductClassId::Cm5, panel(0));
        return r.set ? r.rows : -1;
    }

    void test_the_whole_chain()
    {
        section("the chain, every branch, by bacnet_device_type");

        check_eq(chain(44), 8, "T38AI8AO6DO");
        check_eq(chain(43), 22, "PID_T322AI");
        check_eq(chain(53), 32, "PID_T332AI");
        check_eq(chain(104), 6, "PWM_TRANSDUCER");
        check_eq(chain(46), 12, "PID_T3PT12");
        check_eq(chain(72), 8, "PM_T3_LC");
        check_eq(chain(95), 24, "PID_T36CTA");
        check_eq(chain(4), -1, "TINY_EX_MINIPANEL: not set");
        for (int stm32 = 210; stm32 <= 214; stm32++)
            check_eq(chain(stm32), 3, "an STM32 sensor, 210-214");
        check_eq(chain(215), 64, "215 is not in the STM32 list");
        check_eq(chain(60), 15, "PM_MULTI_SENSOR");
        check_eq(chain(62), 32, "PM_TSTAT_AQ");
        check_eq(chain(65), 32, "PM_AIRLAB_ESP32");
        check_eq(chain(0), 64, "anything else: BAC_INPUT_ITEM_COUNT");
    }

    void test_the_chain()
    {
        section("the row limit follows T3000's chain, model by model");

        check_eq(rows(44), 8, "T3-8AI8AO6DO: 8");
        check_eq(rows(43), 22, "T3-22AI: 22");
        check_eq(rows(53), 32, "T3-32AI: 32");
        check_eq(rows(46), 12, "T3-PT12: 12");
        check_eq(rows(60), 15, "PM_MULTI_SENSOR: 15");
        check_eq(rows(62), 32, "PM_TSTAT_AQ: 32");

        check_eq(rows(0), 64, "a CM5 (mini_type 0): all 64");
        check_eq(rows(1), 64, "a big Minipanel: all 64");
        check_eq(rows(29), 64, "an RMC1232: all 64");
        check_eq(rows(9), 64, "a TSTAT10: all 64");
    }

    void test_the_chip_bits_do_not_change_the_model()
    {
        section("the chip bits are dropped before the chain");

        check_eq(rows(0x80 | 44), 8, "an APM-built T3-8AI8AO6DO is still 8");
        check_eq(rows(0x40 | 43), 22, "a GD-built T3-22AI is still 22");
        check_eq(bacnet_device_type(panel(0xC0 | 53)), 53, "bacnet_device_type is the low six bits");
    }

    void test_codes_the_settings_cannot_reach()
    {
        section("codes above 63 are in the chain, and cannot come from the settings");

        // PWM_TRANSDUCER is 104, PM_T3_LC 72, PID_T36CTA 95, the STM32
        // sensors 210-214, PM_AIRLAB_ESP32 65. Six bits of mini_type cannot
        // hold any of them - 104 arrives as 40, and so on - so a panel whose
        // settings say so gets the default.
        check_eq(rows(104), 64, "104 & 0x3F is 40: not the PWM transducer's 6");
        check_eq(rows(95), 64, "95 & 0x3F is 31: not the T3-6CTA's 24");
        check_eq(rows(72), 64, "72 & 0x3F is 8, a Minipanel ARM NB: 64, not the T3-LC's 8");
        check_eq(rows(210), 64, "210 & 0x3F is 18: not an STM32 sensor's 3");
        check_eq(rows(65), 64, "65 & 0x3F is 1: a big Minipanel, 64");
    }

    void test_the_tiny_ex_minipanel_gets_no_count()
    {
        section("T3000 sets no row limit for a Tiny-EX Minipanel");

        const InputRows r = input_rows(ProductClassId::Cm5, panel(4));
        check(!r.set, "no count: T3000 keeps the previous panel's, 0 on a first open");
    }

    void test_esp32_sizing()
    {
        section("an ESP32 T3 on 63.7 or later sizes its inputs from max_in");

        const ProductClassId esp32 = ProductClassId::Esp32T3Series;

        check_eq(inputs_to_read(esp32, panel(0, 637, 96)), 96, "63.7 with max_in 96: 96");
        check_eq(inputs_to_read(esp32, panel(0, 636, 96)), 64, "63.6: 64, whatever max_in says");
        check_eq(inputs_to_read(esp32, panel(0, 700, 64)), 64, "max_in 64 is not above 64: 64");
        check_eq(inputs_to_read(esp32, panel(0, 700, 65)), 65, "max_in 65: 65");
        check_eq(inputs_to_read(esp32, panel(0, 700, 0)), 64, "max_in 0: 64");
        check_eq(inputs_to_read(esp32, panel(0, 700, 32)), 64,
                 "max_in 32: 64 - T3000 only ever raises the count, never lowers it");
        check_eq(inputs_to_read(esp32, panel(0, 700, 255)), 255, "max_in 255, the most one byte can name");
        check_eq(inputs_to_read(ProductClassId::MiniPanelArm, panel(0, 700, 96)), 64,
                 "any other product: 64");

        check_eq(input_rows(esp32, panel(0, 637, 96)).rows, 96, "and the rows follow");
        check_eq(input_rows(esp32, panel(44, 637, 96)).rows, 8,
                 "unless the model has its own limit, which comes first in the chain");
    }
}

int run_input_rows_tests()
{
    test_the_whole_chain();
    test_the_chain();
    test_the_chip_bits_do_not_change_the_model();
    test_codes_the_settings_cannot_reach();
    test_the_tiny_ex_minipanel_gets_no_count();
    test_esp32_sizing();
    return 0;
}
