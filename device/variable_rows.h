#pragma once

// What a panel's settings decide about its Variables grid: how many
// variables T3000 reads from it, which is also how many rows it shows.
//
// Unlike Inputs and Outputs there is no chain of models: Fresh_Variable_List
// (BacnetVariable.cpp:209-478) has no per-model row limit, and shows every
// row up to variable_item_limit_count (:271).

#include "input_rows.h"
#include "product.h"
#include "../wire/panel.h"

namespace t5000::device
{
    // BAC_VARIABLE_ITEM_COUNT, or, on a panel that sizes its points from its
    // settings, DYNAMIC_VARIABLE_ITEM_COUNT, which is max_var when that is
    // above 128 (global_function.cpp:17673-17687, BacnetView.cpp:4964-4972).
    // At most 255: the index is one byte.
    int variables_to_read(ProductClassId product, const wire::PanelSettings& settings);
}
