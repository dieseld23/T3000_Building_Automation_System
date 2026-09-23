// Proves that the vendored wire format in points.h still matches the layout the
// shipping application uses. Nothing here runs: every check is a static_assert,
// so a mismatch is a build failure rather than corrupted equipment.
//
// This exists because the failure it guards against is silent. The point structs
// are read straight off the wire into memory. If a field moves, nothing throws -
// the tool reports a plausible-looking wrong value for a live building
// controller, and will happily write to that same offset.
//
// It also exists because there are TWO headers in this repo defining structs
// with these names, they disagree, and the stale one is the one that looks
// safe to use. See the comment at the top of points.h.

#include <stddef.h>
#include <stdint.h>

// T3000/CM5/ud_str.h is the live layout. It needs shims to compile outside MFC;
// cm5_header.h has them and explains each.
#include "cm5_header.h"
#include "points.h"

namespace
{
    using t5000::wire::InputPoint;
    using t5000::wire::OutputPoint;
    using t5000::wire::VariablePoint;

    // Size first, for a readable error when the whole struct is wrong.
    static_assert(sizeof(InputPoint) == sizeof(::Str_in_point),
        "points.h InputPoint and CM5 Str_in_point disagree on size - the vendored "
        "wire format has drifted from the application's.");

    // Then every field. This is the check that matters: the stale layout in
    // BacNetDllforVc/include/ud_str.h has the SAME total size while putting
    // sen_on/sen_off and a single calibration byte where the live layout has
    // sub_id/sub_product and calibration_h/calibration_l. Size alone would pass.
#define WIRE_FIELD_MATCHES(Ours, Theirs, field)                                \
    static_assert(offsetof(Ours, field) == offsetof(::Theirs, field),          \
        #Ours "::" #field " is at a different offset than CM5 " #Theirs "::" #field); \
    static_assert(sizeof(Ours::field) == sizeof(::Theirs::field),              \
        #Ours "::" #field " is a different size than CM5 " #Theirs "::" #field)

    WIRE_FIELD_MATCHES(InputPoint, Str_in_point, description);
    WIRE_FIELD_MATCHES(InputPoint, Str_in_point, label);
    WIRE_FIELD_MATCHES(InputPoint, Str_in_point, value);
    WIRE_FIELD_MATCHES(InputPoint, Str_in_point, filter);
    WIRE_FIELD_MATCHES(InputPoint, Str_in_point, decom);
    WIRE_FIELD_MATCHES(InputPoint, Str_in_point, sub_id);
    WIRE_FIELD_MATCHES(InputPoint, Str_in_point, sub_product);
    WIRE_FIELD_MATCHES(InputPoint, Str_in_point, control);
    WIRE_FIELD_MATCHES(InputPoint, Str_in_point, auto_manual);
    WIRE_FIELD_MATCHES(InputPoint, Str_in_point, digital_analog);
    WIRE_FIELD_MATCHES(InputPoint, Str_in_point, calibration_sign);
    WIRE_FIELD_MATCHES(InputPoint, Str_in_point, sub_number);
    WIRE_FIELD_MATCHES(InputPoint, Str_in_point, calibration_h);
    WIRE_FIELD_MATCHES(InputPoint, Str_in_point, calibration_l);
    WIRE_FIELD_MATCHES(InputPoint, Str_in_point, range);

    // Outputs. Same treatment, and it earns it: the stale header puts
    // access_level where the device has hw_switch_status, m_del_low/s_del_high
    // where it has sub_id/sub_product, and a uint16 delay_timer where it has
    // sub_number + pwm_period. It also omits low_voltage/high_voltage
    // entirely. Several of those swaps preserve the total size.
    static_assert(sizeof(OutputPoint) == sizeof(::Str_out_point),
        "points.h OutputPoint and CM5 Str_out_point disagree on size - the vendored "
        "wire format has drifted from the application's.");

    WIRE_FIELD_MATCHES(OutputPoint, Str_out_point, description);
    WIRE_FIELD_MATCHES(OutputPoint, Str_out_point, low_voltage);
    WIRE_FIELD_MATCHES(OutputPoint, Str_out_point, high_voltage);
    WIRE_FIELD_MATCHES(OutputPoint, Str_out_point, label);
    WIRE_FIELD_MATCHES(OutputPoint, Str_out_point, value);
    WIRE_FIELD_MATCHES(OutputPoint, Str_out_point, auto_manual);
    WIRE_FIELD_MATCHES(OutputPoint, Str_out_point, digital_analog);
    WIRE_FIELD_MATCHES(OutputPoint, Str_out_point, hw_switch_status);
    WIRE_FIELD_MATCHES(OutputPoint, Str_out_point, control);
    WIRE_FIELD_MATCHES(OutputPoint, Str_out_point, digital_control);
    WIRE_FIELD_MATCHES(OutputPoint, Str_out_point, decom);
    WIRE_FIELD_MATCHES(OutputPoint, Str_out_point, range);
    WIRE_FIELD_MATCHES(OutputPoint, Str_out_point, sub_id);
    WIRE_FIELD_MATCHES(OutputPoint, Str_out_point, sub_product);
    WIRE_FIELD_MATCHES(OutputPoint, Str_out_point, sub_number);
    WIRE_FIELD_MATCHES(OutputPoint, Str_out_point, pwm_period);

    // Variables. Note CM5/ud_str.h has TWO similarly-named structs -
    // Str_variable_point (:407) is the one the Variables screen uses;
    // Str_variable_uint_point (:286) is a different shape carrying the same
    // wrong "= 40" comment the output struct has. This guard names the former
    // explicitly so a later edit cannot quietly swap them.
    static_assert(sizeof(VariablePoint) == sizeof(::Str_variable_point),
        "points.h VariablePoint and CM5 Str_variable_point disagree on size - the "
        "vendored wire format has drifted from the application's.");

    WIRE_FIELD_MATCHES(VariablePoint, Str_variable_point, description);
    WIRE_FIELD_MATCHES(VariablePoint, Str_variable_point, label);
    WIRE_FIELD_MATCHES(VariablePoint, Str_variable_point, value);
    WIRE_FIELD_MATCHES(VariablePoint, Str_variable_point, auto_manual);
    WIRE_FIELD_MATCHES(VariablePoint, Str_variable_point, digital_analog);
    WIRE_FIELD_MATCHES(VariablePoint, Str_variable_point, control);
    WIRE_FIELD_MATCHES(VariablePoint, Str_variable_point, unused);
    WIRE_FIELD_MATCHES(VariablePoint, Str_variable_point, range);

#undef WIRE_FIELD_MATCHES

    // The description and label lengths are separately #defined in both headers.
    static_assert(t5000::wire::kDescriptionLength == STR_IN_DESCRIPTION_LENGTH,
        "kDescriptionLength no longer matches STR_IN_DESCRIPTION_LENGTH");
    static_assert(t5000::wire::kLabelLength == STR_IN_LABEL,
        "kLabelLength no longer matches STR_IN_LABEL");

    // The output description is the trap. CM5/ud_str.h declares the field as
    // description[STR_OUT_DESCRIPTION_LENGTH-2] while the comment beside it
    // reads "(21 bytes; string)" - so reading the header casually gives 21 and
    // the macro arithmetic gives 19. Assert the arithmetic, not the prose.
    static_assert(t5000::wire::kOutputDescriptionLength == STR_OUT_DESCRIPTION_LENGTH - 2,
        "kOutputDescriptionLength no longer matches STR_OUT_DESCRIPTION_LENGTH - 2");
    static_assert(t5000::wire::kOutputLabelLength == STR_OUT_LABEL,
        "kOutputLabelLength no longer matches STR_OUT_LABEL");
}
