#include "input_rows.h"

#include "../bacnet/point_read.h"

// ProductModel.h has no includes and is safe to include anywhere; it pins the
// PM_* codes the row chain uses.
#include "../../T3000/ProductModel.h"

namespace t5000::device
{
    namespace
    {
        constexpr int t3(MiniType t) { return static_cast<int>(t); }
        constexpr int pm(ProductClassId p) { return static_cast<int>(p); }

        static_assert(pm(ProductClassId::PwmTransducer) == PWM_TRANSDUCER, "PWM_TRANSDUCER");
        static_assert(pm(ProductClassId::T3Lc) == PM_T3_LC, "PM_T3_LC");
        static_assert(pm(ProductClassId::Stm32Co2Net) == STM32_CO2_NET, "STM32_CO2_NET");
        static_assert(pm(ProductClassId::Stm32Co2Rs485) == STM32_CO2_RS485, "STM32_CO2_RS485");
        static_assert(pm(ProductClassId::Stm32HumNet) == STM32_HUM_NET, "STM32_HUM_NET");
        static_assert(pm(ProductClassId::Stm32HumRs485) == STM32_HUM_RS485, "STM32_HUM_RS485");
        static_assert(pm(ProductClassId::Stm32PressureNet) == STM32_PRESSURE_NET, "STM32_PRESSURE_NET");
        static_assert(pm(ProductClassId::MultiSensor) == PM_MULTI_SENSOR, "PM_MULTI_SENSOR");
        static_assert(pm(ProductClassId::TstatAq) == PM_TSTAT_AQ, "PM_TSTAT_AQ");
        static_assert(pm(ProductClassId::AirlabEsp32) == PM_AIRLAB_ESP32, "PM_AIRLAB_ESP32");
        static_assert(pm(ProductClassId::Esp32T3Series) == PM_ESP32_T3_SERIES, "PM_ESP32_T3_SERIES");
    }

    int bacnet_device_type(const wire::PanelSettings& settings)
    {
        return settings.mini_type();
    }

    bool sizes_points_from_settings(ProductClassId product, const wire::PanelSettings& settings)
    {
        return product == ProductClassId::Esp32T3Series && settings.firmware() >= kEsp32IoCountRedefineVersion;
    }

    int inputs_to_read(ProductClassId product, const wire::PanelSettings& settings)
    {
        // DYNAMIC_INPUT_ITEM_COUNT starts at 64 (global_variable.h:2379) and
        // is raised only when max_in is larger. T3000 never lowers it, so a
        // panel with max_in <= 64 opened after one with more gets the larger
        // count; T5000 uses 64, as T3000 does on a first open.
        if (sizes_points_from_settings(product, settings) && settings.max_in > bacnet::kInputCount)
            return settings.max_in;
        return bacnet::kInputCount;
    }

    InputRows input_rows(ProductClassId product, const wire::PanelSettings& settings)
    {
        return input_rows_for(bacnet_device_type(settings), product, settings);
    }

    InputRows input_rows_for(int type, ProductClassId product, const wire::PanelSettings& settings)
    {
        // One chain, one variable, two numbering schemes: T3_* panel types
        // (global_define.h:1305-1338) and PM_* product codes (ProductModel.h).
        // They share values - T3_TSTAT10 is 9, as is PM_TSTAT8 - and T3000
        // gets away with it because the mini_type of a panel on this path is
        // never a Tstat code.
        InputRows r;

        if (type == t3(MiniType::T38AI8AO6DO))
            r.rows = 8;
        else if (type == t3(MiniType::T322AI))
            r.rows = 22;
        else if (type == t3(MiniType::T332AI))
            r.rows = 32;
        else if (type == pm(ProductClassId::PwmTransducer))
            r.rows = 6;
        else if (type == t3(MiniType::T3PT12))
            r.rows = 12;
        else if (type == pm(ProductClassId::T3Lc))
            r.rows = 8;
        else if (type == t3(MiniType::T36CTA))
            r.rows = 24;
        else if (type == t3(MiniType::TinyExMiniPanel))
            r.set = false;   // :771-775 - the assignment is commented out
        else if (type == pm(ProductClassId::Stm32Co2Net) || type == pm(ProductClassId::Stm32Co2Rs485) ||
                 type == pm(ProductClassId::Stm32HumNet) || type == pm(ProductClassId::Stm32HumRs485) ||
                 type == pm(ProductClassId::Stm32PressureNet))
            r.rows = 3;
        else if (type == pm(ProductClassId::MultiSensor))
            r.rows = 15;
        else if (type == pm(ProductClassId::TstatAq) || type == pm(ProductClassId::AirlabEsp32))
            r.rows = 32;
        else
            r.rows = inputs_to_read(product, settings);

        return r;
    }
}
