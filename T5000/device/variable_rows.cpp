#include "variable_rows.h"

#include "../bacnet/point_read.h"

namespace t5000::device
{
    int variables_to_read(ProductClassId product, const wire::PanelSettings& settings)
    {
        // DYNAMIC_VARIABLE_ITEM_COUNT starts at 128 (global_variable.h:2381)
        // and T3000 never lowers it; as inputs_to_read, T5000 uses what a
        // first open would.
        if (sizes_points_from_settings(product, settings) && settings.max_var > bacnet::kVariableCount)
            return settings.max_var;
        return bacnet::kVariableCount;
    }
}
