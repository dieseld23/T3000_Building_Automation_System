#pragma once

// Turning bytes off the wire into point structs.
//
// This mirrors fill_in_input in T3000/global_function.cpp:3467, which is the
// shipping decoder and therefore the definition of correct. It is reimplemented
// rather than reused for the same reason points.h vendors the struct: the
// original lives in a translation unit that includes fifteen dialog headers.
//
// The field order below is a second, independent confirmation of points.h.
// fill_in_input walks the buffer one field at a time, and the order it walks -
// description, label, value, filter, decom, sub_id, sub_product, control,
// auto_manual, digital_analog, calibration_sign, sub_number, calibration_h,
// calibration_l, range - is exactly the order of InputPoint. Two unrelated
// places in the codebase agreeing on the layout is worth more than either alone.

#include <stddef.h>
#include <stdint.h>

#include "points.h"

namespace t5000::wire
{
    // What one input point occupies on the wire. Equal to sizeof(InputPoint)
    // because the struct is #pragma pack(1) and the fields are in wire order,
    // but stated separately so the two can be checked against each other rather
    // than one silently defining the other.
    inline constexpr size_t kInputPointWireSize = 46;

    static_assert(kInputPointWireSize == sizeof(InputPoint),
        "The wire size and the packed struct size have diverged.");

    // Replaces '-' and '.' with '_' in a fixed-width label, in place.
    //
    // fill_in_input does this by widening to UTF-16, calling CString::Replace
    // twice and narrowing back. Operating on the bytes directly is equivalent
    // and is safe even though these labels are CP_ACP rather than ASCII: the
    // device text is GBK, whose trail bytes are all >= 0x40, so a 0x2D ('-') or
    // 0x2E ('.') byte can never be the second half of a double-byte character.
    void sanitize_label(uint8_t* label, size_t length);

    // Decodes one input point. Returns false if the buffer is too short, rather
    // than reading past it - the length here comes from a device response, so it
    // is not something to take on trust.
    //
    // A memcpy of the whole struct would happen to work on x86, since the layout
    // is packed and in wire order. It is decoded field by field anyway, so that
    // the code says what the wire format is instead of depending on the compiler
    // agreeing with it.
    bool decode_input_point(const uint8_t* buffer, size_t length, InputPoint& out);

    // What one output point occupies on the wire: 45, not 46 - see
    // OutputPoint in points.h.
    inline constexpr size_t kOutputPointWireSize = 45;
    static_assert(kOutputPointWireSize == sizeof(OutputPoint),
        "The wire size and the packed struct size have diverged.");

    // Decodes one output point, as fill_in_output (global_function.cpp:3401)
    // does - which is not quite as fill_in_input does inputs. The two differ
    // over text that does not fit its field, and this follows the output
    // decoder: see decode.cpp.
    bool decode_output_point(const uint8_t* buffer, size_t length, OutputPoint& out);
}
