// Tests for how many variables T3000 reads from a panel, against the
// sizing at global_function.cpp:17673-17687 and BacnetView.cpp:4963-4971.

#include "variable_rows.h"
#include "../testing/check.h"

namespace
{
    using namespace t5000::device;
    using namespace t5000::testing;
    using t5000::wire::PanelSettings;

    PanelSettings panel(int firmware, uint8_t max_var)
    {
        PanelSettings s;
        s.mini_type_byte = 1;
        s.firmware_main  = (uint8_t)(firmware / 10);
        s.firmware_sub   = (uint8_t)(firmware % 10);
        s.max_var        = max_var;
        return s;
    }

    void test_how_many_variables_are_read()
    {
        section("128 variables, or max_var on an ESP32 T3 on firmware 63.7 or later");

        const ProductClassId esp32 = ProductClassId::Esp32T3Series;
        check_eq(variables_to_read(esp32, panel(637, 200)), 200, "an ESP32 on 63.7 with max_var 200: 200");
        check_eq(variables_to_read(esp32, panel(637, 255)), 255, "  max_var 255: 255");
        check_eq(variables_to_read(esp32, panel(637, 129)), 129, "  max_var 129: 129");
        check_eq(variables_to_read(esp32, panel(637, 128)), 128, "  max_var 128: 128");
        check_eq(variables_to_read(esp32, panel(637, 127)), 128, "  max_var 127: 128, not 127");
        check_eq(variables_to_read(esp32, panel(637, 0)), 128, "  max_var 0: 128");
        check_eq(variables_to_read(esp32, panel(636, 200)), 128, "on 63.6: 128, whatever max_var says");
        check_eq(variables_to_read(ProductClassId::MiniPanelArm, panel(637, 200)), 128,
                 "any other product: 128");
    }
}

int run_variable_rows_tests()
{
    test_how_many_variables_are_read();
    return 0;
}
