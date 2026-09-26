#pragma once

// A panel's inputs as the operator configures them before the device can be
// reached: T3000's rules for changing an input, applied to the bytes of its
// Str_in_point.
//
// Nothing here can send anything to a device. It changes bytes that T5000
// keeps (in T5000.db, store/device_db.h), and it includes nothing from bacnet/,
// discovery/ or net/, which it must not: a configuration made here reaches a
// device only through the write path, which does not exist yet and which
// will compare it with the device first.
//
// Bytes, not the decoded struct, because the bytes are what T3000 sends when
// it writes an input (WRITEINPUT_T3000, the whole Str_in_point). The page
// shows them through decode_input_point, so it shows what T3000 would show
// after reading them back.

#include <stdint.h>

#include <array>
#include <string>
#include <vector>

#include "../wire/decode.h"

namespace t5000::offline
{
    using InputBytes = std::array<uint8_t, wire::kInputPointWireSize>;

    // Where each field of Str_in_point (ud_str.h:288-306) sits, as
    // decode_input_point reads them.
    namespace input_at
    {
        inline constexpr size_t description      = 0;    // 21 bytes
        inline constexpr size_t label            = 21;   // 9 bytes
        inline constexpr size_t value            = 30;   // 4 bytes, little-endian
        inline constexpr size_t filter           = 34;
        inline constexpr size_t decom            = 35;   // low nibble status, high nibble signal type
        inline constexpr size_t sub_id           = 36;
        inline constexpr size_t sub_product      = 37;
        inline constexpr size_t control          = 38;
        inline constexpr size_t auto_manual      = 39;
        inline constexpr size_t digital_analog   = 40;
        inline constexpr size_t calibration_sign = 41;
        inline constexpr size_t sub_number       = 42;
        inline constexpr size_t calibration_h    = 43;
        inline constexpr size_t calibration_l    = 44;
        inline constexpr size_t range            = 45;
    }

    // What T3000 gives input `index` (0-based) of a panel it has not read:
    // named IN1, IN2 and so on, filter 5, every other byte 0
    // (Initial_All_Point, global_function.cpp:17716-17726). The same for
    // every product and model.
    InputBytes default_input(int index);

    // The columns that can be changed offline so far. Range, and the columns
    // that depend on it - Value, Calibration, Sign, Signal Type - come with
    // the range dialog.
    enum class InputField
    {
        FullLabel,
        Label,
        AutoManual,
        Filter,
    };

    // The payload's names for them: "fullLabel", "label", "autoManual",
    // "filter".
    const char* input_field_name(InputField field);
    bool input_field_from_name(const std::string& name, InputField& field);

    // Every field that can be changed, in the grid's order.
    std::vector<InputField> editable_input_fields();

    // Changes one input as T3000's grid does (Fresh_Input_Item,
    // BacnetInput.cpp:451-706, and the Auto/Manual click at :1615-1654).
    //
    // `inputs` is every input the panel has, as configured; `rows` is how
    // many T3000 shows (INPUT_LIMITE_ITEM_COUNT), and only those can be
    // changed (:458). `text` is what the operator typed, as UTF-8, or for
    // Auto/Manual the state they chose: "Auto" or "Manual".
    //
    // Refused, with `message` saying why and `inputs` as it was, for text
    // that is too long or already names another point, a filter outside
    // 0-255, and anything that is not one of these fields' values.
    // `changed` is false when the input already is what was asked: T3000
    // writes nothing then either (:694-701).
    //
    // Text is stored in `code_page`: this computer's ANSI code page (CP_ACP,
    // 0) unless a test names another, as T3000 stores it and as the page
    // shows device text.
    bool apply_input_edit(std::vector<InputBytes>& inputs, int rows, int index, InputField field,
                          const std::string& text, bool& changed, std::string& message,
                          unsigned code_page = 0);

    // The payload's names for the columns whose bytes differ between the two,
    // in the grid's order: what the page marks as changed. Columns share
    // bytes - a digital input's Value is its control byte - so a name is
    // given for each column its bytes feed.
    std::vector<std::string> changed_fields(const InputBytes& base, const InputBytes& edited);

    // Text as T3000 stores it: UTF-8 to UTF-16 to the code page, which is
    // what WideCharToMultiByte(CP_ACP) gives T3000 when `code_page` is 0.
    // False, with `message` saying why, for text that is not valid UTF-8 or
    // holds a character the code page cannot store, which T3000 would store
    // as "?" or as a look-alike.
    bool utf8_to_code_page(const std::string& utf8, unsigned code_page, std::string& out, std::string& message);
}
