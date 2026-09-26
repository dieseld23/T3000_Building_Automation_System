#include "output_rows.h"

#include "../bacnet/point_read.h"

// As in input_rows.cpp, the chain mixes T3_* panel types and PM_* product
// codes in one variable. conformance/product_guard.cpp and tables_guard.cpp
// check both against T3000's headers.

namespace t5000::device
{
    namespace
    {
        constexpr int t3(MiniType t) { return static_cast<int>(t); }
        constexpr int pm(ProductClassId p) { return static_cast<int>(p); }

        OutputRows physical(OutputCounts counts)
        {
            OutputRows r;
            r.rows    = bacnet::kOutputCount;
            r.special = counts;
            return r;
        }

        // Rows = the model's outputs, and none of them external.
        OutputRows exactly(OutputCounts counts)
        {
            OutputRows r;
            r.rows           = counts.digital + counts.analog;
            r.special        = counts;
            r.shows_external = false;
            return r;
        }
    }

    int outputs_to_read(ProductClassId product, const wire::PanelSettings& settings)
    {
        // DYNAMIC_OUTPUT_ITEM_COUNT starts at 64 (global_variable.h:2380)
        // and T3000 never lowers it; as inputs_to_read, T5000 uses what a
        // first open would.
        if (sizes_points_from_settings(product, settings) && settings.max_out > bacnet::kOutputCount)
            return settings.max_out;
        return bacnet::kOutputCount;
    }

    OutputRows output_rows(ProductClassId product, const wire::PanelSettings& settings)
    {
        return output_rows_for(bacnet_device_type(settings), product, settings);
    }

    OutputRows output_rows_for(int type, ProductClassId product, const wire::PanelSettings& settings)
    {
        OutputRows r;
        r.rows = bacnet::kOutputCount;

        if (type == t3(MiniType::BigMiniPanel) || type == t3(MiniType::MiniPanelArm))
            r = physical(kBigMiniPanelOutputs);
        else if (type == t3(MiniType::SmallMiniPanel) || type == t3(MiniType::MiniPanelArmLb))
            r = physical(kSmallMiniPanelOutputs);
        else if (type == t3(MiniType::TinyMiniPanel))
            r = physical(kTinyMiniPanelOutputs);
        else if (type == t3(MiniType::TinyExMiniPanel) || type == t3(MiniType::MiniPanelArmTb))
            r = physical(kTinyExMiniPanelOutputs);
        else if (type == t3(MiniType::EspLw))
            r = physical(kEspLwOutputs);
        else if (type == t3(MiniType::Tb11I))
            r = physical(kTb11IOutputs);
        else if (type == pm(ProductClassId::T38AI8AO6DO))
            r = exactly(kT38AI8AO6DOOutputs);
        else if (type == pm(ProductClassId::T322AI))
            r = exactly(kT322AIOutputs);
        else if (type == pm(ProductClassId::T332AIArm))
            r = exactly(kT332AIOutputs);
        else if (type == pm(ProductClassId::PwmTransducer))
            r = exactly(kPwmTransducerOutputs);
        else if (type == pm(ProductClassId::Stm32Co2Net) || type == pm(ProductClassId::Stm32Co2Rs485) ||
                 type == pm(ProductClassId::Stm32HumNet) || type == pm(ProductClassId::Stm32HumRs485) ||
                 type == pm(ProductClassId::Stm32PressureNet) || type == pm(ProductClassId::Stm32PressureRs485))
        {
            // Six, where the Inputs chain has five: this one includes
            // STM32_PRESSURE_RS485 (:498-503).
            r.rows           = 3;
            r.shows_external = false;
        }
        else if (type == pm(ProductClassId::T3Lc))
            r.rows = 8;
        else if (type == t3(MiniType::T36CTA))
            r.rows = 2;
        else if (type == t3(MiniType::EspRmc))
            r.special = kRmcOutputs;
        else if (type == t3(MiniType::Rmc1232))
            r.special = kRmc1232Outputs;
        else if (type == t3(MiniType::Bms))
            r.special = kBmsOutputs;
        else if (type == t3(MiniType::Ng3))
            r.special = kNg3Outputs;

        // After the chain, and over it: an ESP32 T3 on firmware 63.7 or later
        // shows DYNAMIC_OUTPUT_ITEM_COUNT rows whatever its model, keeping the
        // model's switches and whether it shows external outputs
        // (:540-544).
        if (sizes_points_from_settings(product, settings))
            r.rows = outputs_to_read(product, settings);

        return r;
    }

    bool shows_hoa_switch(int type)
    {
        switch (type)
        {
        case t3(MiniType::Tstat10):
        case t3(MiniType::Tstat11):
        case t3(MiniType::Oem):
        case t3(MiniType::Oem12I):
        case t3(MiniType::EspRmc):
        case t3(MiniType::Rmc1232):
        case t3(MiniType::Bms):
        case t3(MiniType::Ng3):
        case t3(MiniType::ThreeIic):
        case t3(MiniType::BigMiniPanel):
        case t3(MiniType::MiniPanelArm):
        case t3(MiniType::MiniPanelArmLb):
        case t3(MiniType::EspLw):
        case t3(MiniType::Tb11I):
        case t3(MiniType::MiniPanelArmTb):
        case t3(MiniType::SmallMiniPanel):
        case t3(MiniType::TinyMiniPanel):
        case t3(MiniType::TinyExMiniPanel):
        case t3(MiniType::T38AI8AO6DO):
        case t3(MiniType::T322AI):
        case pm(ProductClassId::PwmTransducer):
            return true;
        default:
            return false;
        }
    }
}
