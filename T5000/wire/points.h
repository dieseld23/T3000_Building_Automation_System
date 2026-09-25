#pragma once

// The on-the-wire layout of a device's configuration points.
//
// This is a deliberate copy of the structs in T3000/CM5/ud_str.h rather than an
// include of them, because that header cannot compile outside an MFC
// translation unit: it uses `byte`, which only exists once the Windows headers
// have been pulled in, and puts a `public:` inside Point_T3000.
//
// Copying a wire format is normally a mistake. It is done here because the
// alternative is worse, and because the copy is checked rather than trusted -
// conformance/wire_guard.cpp includes BOTH this header and the real one and
// fails the build of T3000's solution if any field moves. Treat that guard as
// part of this file.
//
// DO NOT "fix" this by including BacNetDllforVc/include/ud_str.h instead. That
// header defines structs with these same names and it is STALE: it still has
// sen_on/sen_off where the device has sub_id/sub_product, and a single
// calibration byte where the device has calibration_h/calibration_l. It is also
// the copy that looks clean - no MFC anywhere near it - which is exactly what
// makes it dangerous. Building on it produces a tool that compiles, looks
// right, and misreads every point on real hardware while writing to it.
//
// The live layout is the one T3000/global_variable.h:5 includes: CM5\ud_str.h.

#include <stdint.h>

namespace t5000::wire
{
    // Byte-packed to match the device. CM5/ud_str.h wraps these in
    // #pragma pack(1) and the structs are read straight off the wire, so any
    // padding introduced here is silent corruption.
#pragma pack(push, 1)

    enum : int
    {
        kDescriptionLength = 21,   // STR_IN_DESCRIPTION_LENGTH
        kLabelLength       = 9,    // STR_IN_LABEL
    };

    struct InputPoint
    {
        uint8_t  description[kDescriptionLength];
        uint8_t  label[kLabelLength];
        int32_t  value;

        uint8_t  filter;

        // Two fields in one byte, and not "decommissioned" whatever the name
        // says. Low nibble: 0 normal, 1 open circuit, 2 short circuit. High
        // nibble: the signal type (jumper). BacnetInput.cpp:1179-1180.
        uint8_t  decom;
        uint8_t  sub_id;
        uint8_t  sub_product;
        uint8_t  control;
        uint8_t  auto_manual;        // 0 = auto, 1 = manual
        uint8_t  digital_analog;     // 1 = analog, 0 = digital

        // Calibration is a sign plus a high/low byte pair, not a plain int.
        // The stale header has one `calibration` byte here instead, which is
        // the single most likely way for a copy of this struct to go wrong.
        uint8_t  calibration_sign;   // 0 = positive, 1 = negative
        uint8_t  sub_number;
        uint8_t  calibration_h;
        uint8_t  calibration_l;

        uint8_t  range;              // input_range_equate
    };

    enum : int
    {
        // Not a typo, and not the same as the input lengths. CM5/ud_str.h
        // declares this field as description[STR_OUT_DESCRIPTION_LENGTH-2]
        // with the macro at 21, so the array is 19 - while the comment beside
        // it in that same header says "(21 bytes; string)". The comment is
        // wrong. conformance/wire_guard.cpp asserts the arithmetic rather than
        // the prose.
        kOutputDescriptionLength = 19,   // STR_OUT_DESCRIPTION_LENGTH - 2
        kOutputLabelLength       = 9,    // STR_OUT_LABEL
    };

    struct OutputPoint
    {
        uint8_t  description[kOutputDescriptionLength];

        // Inputs have no equivalent of these two. They sit between the
        // description and the label, which is the kind of placement that makes
        // a field-by-field guard worth having.
        uint8_t  low_voltage;
        uint8_t  high_voltage;

        uint8_t  label[kOutputLabelLength];
        int32_t  value;

        uint8_t  auto_manual;        // 0 = auto, 1 = manual
        uint8_t  digital_analog;     // 1 = analog, 0 = digital

        // SW_OFF = 0, SW_AUTO = 1, SW_HAND = 2. The physical switch on the
        // board, not a software setting - it reports where a human left it.
        // The stale header has access_level in this slot instead.
        uint8_t  hw_switch_status;

        uint8_t  control;
        uint8_t  digital_control;
        uint8_t  decom;              // 0 = ok, 1 = decommissioned
        uint8_t  range;              // output_range_equate

        uint8_t  sub_id;
        uint8_t  sub_product;
        uint8_t  sub_number;
        uint8_t  pwm_period;
    };

    struct VariablePoint
    {
        // Plain [21] here, declared literally rather than through a macro -
        // so this one does NOT have the STR_OUT_DESCRIPTION_LENGTH-2 trap that
        // makes the output description 19. Checked, not assumed.
        uint8_t  description[21];       // STR_VARIABLE_DESCRIPTION_LENGTH
        uint8_t  label[9];              // STR_VARIABLE_LABEL
        int32_t  value;

        uint8_t  auto_manual;           // 0 = auto, 1 = manual
        uint8_t  digital_analog;        // 1 = analog, 0 = digital
        uint8_t  control;
        uint8_t  unused;                // present on the wire; keep the hole
        uint8_t  range;                 // variable_range_equate
    };

#pragma pack(pop)

    // 21 + 9 + 4 + 12 = 46. global_define.h:441 records the same number next to
    // BAC_INPUT_ITEM_COUNT. Size agreement is necessary but NOT sufficient - the
    // stale layout also totals 46 - so conformance/wire_guard.cpp checks every
    // offset.
    static_assert(sizeof(InputPoint) == 46, "InputPoint must stay 46 bytes on the wire");

    // 19 + 2 + 9 + 4 + 11 = 45. NOT 46, and not the 40 that CM5/ud_str.h's own
    // trailing comment claims - that comment reads "21+9+4+2+2+2 = 40" and the
    // identical wrong comment also sits in the stale header. Two wrong comments
    // agreeing is not corroboration. This number came from the compiler.
    static_assert(sizeof(OutputPoint) == 45, "OutputPoint must stay 45 bytes on the wire");

    // 21 + 9 + 4 + 5 = 39. CM5/ud_str.h:407 says "39 char" and is right this
    // time - but note Str_variable_uint_point at :286 carries the same bogus
    // "21+9+4+2+2+2 = 40" comment the output struct has. Do not confuse them.
    static_assert(sizeof(VariablePoint) == 39, "VariablePoint must stay 39 bytes on the wire");

    inline int calibration(const InputPoint& p)
    {
        const int magnitude = (p.calibration_h << 8) | p.calibration_l;
        return p.calibration_sign == 1 ? -magnitude : magnitude;
    }
}
