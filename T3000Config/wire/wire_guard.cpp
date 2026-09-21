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

// T3000/CM5/ud_str.h is the live layout, but it does not compile on its own:
//
//   - `byte` is used for several schedule fields and is only defined once the
//     Windows headers are in scope. Defining it here is what lets the real
//     header be included without dragging in windows.h and MFC.
//   - Point_T3000 contains a `public:` access specifier, so the header has to be
//     compiled as C++ regardless.
//
// Both are shallow, and fixing them upstream would mean editing a GBK-encoded
// header that scripted edits have corrupted in this repo before. Shimming is
// the lower-risk option while this guard is the only thing that needs it.
//
// The shims below exist ONLY so the header parses in this one translation unit.
// None of them appears in Str_in_point, which is all this file asserts about, so
// getting a shim's size wrong cannot make a wrong layout pass - it would fail
// the offsetof checks instead. This TU must never include real MFC.
typedef unsigned char  byte;    // ud_str.h:419-422, 445-446, 456, 497-498
typedef unsigned short WORD;    // ud_str.h:499, normally from windef.h

// ud_str.h:1224 puts an MFC CString inside register_point - a struct whose own
// comment says "(size = 6 bytes)". It is not a wire struct despite reading like
// one, and nothing here depends on it; the placeholder just lets the file parse.
struct CString { void* opaque; };

#include "../../T3000/CM5/ud_str.h"
#include "points.h"

namespace
{
    using t3000::wire::InputPoint;

    // Size first, for a readable error when the whole struct is wrong.
    static_assert(sizeof(InputPoint) == sizeof(::Str_in_point),
        "points.h InputPoint and CM5 Str_in_point disagree on size - the vendored "
        "wire format has drifted from the application's.");

    // Then every field. This is the check that matters: the stale layout in
    // BacNetDllforVc/include/ud_str.h has the SAME total size while putting
    // sen_on/sen_off and a single calibration byte where the live layout has
    // sub_id/sub_product and calibration_h/calibration_l. Size alone would pass.
#define WIRE_FIELD_MATCHES(field)                                              \
    static_assert(offsetof(InputPoint, field) == offsetof(::Str_in_point, field), \
        "InputPoint::" #field " is at a different offset than CM5 Str_in_point::" #field); \
    static_assert(sizeof(InputPoint::field) == sizeof(::Str_in_point::field),  \
        "InputPoint::" #field " is a different size than CM5 Str_in_point::" #field)

    WIRE_FIELD_MATCHES(description);
    WIRE_FIELD_MATCHES(label);
    WIRE_FIELD_MATCHES(value);
    WIRE_FIELD_MATCHES(filter);
    WIRE_FIELD_MATCHES(decom);
    WIRE_FIELD_MATCHES(sub_id);
    WIRE_FIELD_MATCHES(sub_product);
    WIRE_FIELD_MATCHES(control);
    WIRE_FIELD_MATCHES(auto_manual);
    WIRE_FIELD_MATCHES(digital_analog);
    WIRE_FIELD_MATCHES(calibration_sign);
    WIRE_FIELD_MATCHES(sub_number);
    WIRE_FIELD_MATCHES(calibration_h);
    WIRE_FIELD_MATCHES(calibration_l);
    WIRE_FIELD_MATCHES(range);

#undef WIRE_FIELD_MATCHES

    // The description and label lengths are separately #defined in both headers.
    static_assert(t3000::wire::kDescriptionLength == STR_IN_DESCRIPTION_LENGTH,
        "kDescriptionLength no longer matches STR_IN_DESCRIPTION_LENGTH");
    static_assert(t3000::wire::kLabelLength == STR_IN_LABEL,
        "kLabelLength no longer matches STR_IN_LABEL");
}
