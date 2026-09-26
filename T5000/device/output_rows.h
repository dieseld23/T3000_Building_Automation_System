#pragma once

// What a panel's settings decide about its Outputs grid: how many outputs
// T3000 reads from it, how many rows it shows, and which of them have a
// hand-off-auto switch.

#include "input_rows.h"
#include "product.h"
#include "../wire/panel.h"

namespace t5000::device
{
    // A model's physical outputs, digital then analog: T3000's *_OUT_D and
    // *_OUT_A (global_define.h:1362-1425). The tables guard checks each
    // against that header, which cannot be included.
    struct OutputCounts
    {
        int digital = 0;
        int analog  = 0;
    };

    inline constexpr OutputCounts kBigMiniPanelOutputs   = { 12, 12 };  // BIG_MINIPANEL_OUT_*
    inline constexpr OutputCounts kSmallMiniPanelOutputs = { 6, 4 };    // SMALL_MINIPANEL_OUT_*
    inline constexpr OutputCounts kTinyMiniPanelOutputs  = { 6, 2 };    // TINY_MINIPANEL_OUT_*
    inline constexpr OutputCounts kTinyExMiniPanelOutputs = { 8, 6 };   // TINYEX_MINIPANEL_OUT_*
    inline constexpr OutputCounts kEspLwOutputs          = { 0, 6 };    // T3_ESP_LW_OUT_*
    inline constexpr OutputCounts kTb11IOutputs          = { 6, 5 };    // T3_TB_11I_OUT_*
    inline constexpr OutputCounts kT38AI8AO6DOOutputs    = { 6, 8 };    // T38AI8AO6DO_OUT_*
    inline constexpr OutputCounts kT322AIOutputs         = { 0, 0 };    // T322AI_OUT_*
    inline constexpr OutputCounts kT332AIOutputs         = { 0, 0 };    // T332AI_OUT_*
    inline constexpr OutputCounts kPwmTransducerOutputs  = { 0, 6 };    // PWM_TRANSDUCER_OUT_*
    inline constexpr OutputCounts kRmcOutputs            = { 7, 0 };    // RMC_OUT_*
    inline constexpr OutputCounts kRmc1232Outputs        = { 4, 0 };    // RMC1232_OUT_*
    inline constexpr OutputCounts kBmsOutputs            = { 0, 0 };    // T3_BMS_OUT_*
    inline constexpr OutputCounts kNg3Outputs            = { 8, 4 };    // NG3_OUT_*

    // How many outputs T3000 reads: BAC_OUTPUT_ITEM_COUNT, or, on a panel
    // that sizes its points from its settings, DYNAMIC_OUTPUT_ITEM_COUNT,
    // which is max_out when that is above 64 (global_function.cpp:17655-17670,
    // BacnetView.cpp:4963-4969). At most 255: the index is one byte. It is
    // also how many rows the grid has at all (output_item_limit_count).
    int outputs_to_read(ProductClassId product, const wire::PanelSettings& settings);

    struct OutputRows
    {
        // OUTPUT_LIMITE_ITEM_COUNT: rows at or past it are shown with every
        // cell empty (BacnetOutput.cpp:684-691). 0 is a real answer: a
        // T3-22AI has no outputs.
        int rows = 0;

        // digital_special_output_count and analog_special_output_count: the
        // outputs, from the first, that a hand-off-auto switch can override
        // on a panel that has them. 0 for most models.
        OutputCounts special;

        // Minipanel_device: whether an output on a sub-device can be shown
        // as an external one (:1000-1003).
        bool shows_external = true;
    };

    // The chain at the top of Fresh_Output_List (BacnetOutput.cpp:418-545).
    OutputRows output_rows(ProductClassId product, const wire::PanelSettings& settings);

    // The chain itself, for any bacnet_device_type. As with input_rows_for,
    // several codes it tests are above 63 and can never come from the
    // settings; exposed so the chain can be tested as the port it is.
    OutputRows output_rows_for(int device_type, ProductClassId product, const wire::PanelSettings& settings);

    // The panel types whose Outputs grid has a HOA Switch column that says
    // anything (BacnetOutput.cpp:733-755). On any other, T3000 leaves the
    // column empty.
    bool shows_hoa_switch(int device_type);
}
