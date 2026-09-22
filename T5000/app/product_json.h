#pragma once

// Serialises what the tool knows about products.
//
// This exists so the capability table is visible rather than implicit. A
// technician meeting a device the tool does not handle should be able to see
// that fact, and see WHY - "not yet verified" and "T3000 never implemented
// this" are different problems with different answers, and a blank screen
// says neither.

#include <string>

namespace t5000::app
{
    // The whole capability table, for a product list or a support matrix.
    std::string build_products_json();

    // One product, including its panel-type detail when a mini_type is known.
    //
    // mini_type is passed as the raw byte read off the device rather than as
    // a MiniType, because the value may not be one we recognise - and an
    // unrecognised panel type is exactly the case worth reporting rather than
    // coercing into an enumerator.
    std::string build_product_json(int product_class_id, int mini_type);
}
