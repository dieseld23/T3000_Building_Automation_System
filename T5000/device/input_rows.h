#pragma once

// What a panel's settings decide about its Inputs grid: how many inputs
// T3000 reads from it, and how many rows it shows.

#include "product.h"
#include "../wire/panel.h"

namespace t5000::device
{
    // ESP32_IO_COUNT_REDEFINE_VERSION (global_define.h:3058). The tables
    // guard checks it, since that header cannot be included.
    inline constexpr int kEsp32IoCountRedefineVersion = 637;

    // T3000's bacnet_device_type once the settings are read: the low six bits
    // of mini_type, assigned even when they are 0 (global_function.cpp:5270).
    //
    // A plain int rather than a MiniType, because that is what it is in
    // T3000: input_rows() compares it with T3_* panel types and PM_* product
    // codes in one chain. Keeping it untyped here keeps that mix visible at
    // the one place it happens, rather than letting it leak into MiniType.
    int bacnet_device_type(const wire::PanelSettings& settings);

    // An ESP32 T3 on firmware 63.7 or later sizes its point lists from its
    // settings (ESP32_IO_COUNT_REDEFINE_VERSION, global_define.h:3058).
    bool sizes_points_from_settings(ProductClassId product, const wire::PanelSettings& settings);

    // How many inputs T3000 reads: BAC_INPUT_ITEM_COUNT, or, on a panel that
    // sizes its points from its settings, DYNAMIC_INPUT_ITEM_COUNT, which is
    // max_in when that is above 64 (global_function.cpp:17634-17636,
    // BacnetView.cpp:4963-4966). At most 255: the index is one byte.
    int inputs_to_read(ProductClassId product, const wire::PanelSettings& settings);

    struct InputRows
    {
        // False for the one model T3000 gives no count: it keeps whatever the
        // previous panel left, which is 0 - every row blank - when it is the
        // first panel opened since T3000 started.
        bool set  = true;
        int  rows = 0;
    };

    // INPUT_LIMITE_ITEM_COUNT, from the chain in Fresh_Input_List
    // (BacnetInput.cpp:736-807). Rows at or past it are shown with every
    // cell empty (:942-949).
    InputRows input_rows(ProductClassId product, const wire::PanelSettings& settings);

    // The chain itself, for any bacnet_device_type. Several codes it tests
    // are above 63 and so can never come from the settings; they reach it in
    // T3000 by other routes (BacnetView.cpp:3776-3792), on products T5000
    // does not read. Exposed so the chain can be tested as the port it is.
    InputRows input_rows_for(int device_type, ProductClassId product, const wire::PanelSettings& settings);
}
