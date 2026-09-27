#pragma once

// A .prog file, as T3000's Save File writes it (SaveBacnetBinaryFile,
// global_function.cpp:12724), read for the one part T5000 imports: a panel's
// inputs, and the settings that say which panel they are.
//
// Nothing here reads a file from disk or sends anything to a device. It
// reads bytes the page was given, and says what an import would keep.
//
// The file is every table T3000 holds for a panel, one after another, each
// as the panel sends it: 55 FF, the format's version, then 64 inputs, 64
// outputs, 128 variables, 16 programs, 16 PID loops, 16 screens, 240
// graphic labels, 8 logins, 8 custom units, 5 analog tables, the 400-byte
// settings, and on to the program code. Versions 6, 7 and 8 each add a
// table at the end. T5000Conformance holds each offset and length to T3000's
// source and to the sizes of its structs.

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <utility>
#include <vector>

#include "../wire/panel.h"
#include "input_edit.h"

namespace t5000::offline
{
    // The versions T3000 reads as this format (LoadBacnetBinaryFile,
    // global_function.cpp:10545-10553): 5 and up. Below 5 was an INI file,
    // kept by an older T3000; 8 is the version it writes today. A later one
    // would have tables this does not know, so it is not guessed at.
    inline constexpr int kFirstProgVersion = 5;
    inline constexpr int kLastProgVersion  = 8;

    namespace prog_at
    {
        inline constexpr size_t version  = 2;
        inline constexpr size_t inputs   = 3;       // 64 Str_in_point
        inline constexpr size_t settings = 30504;   // Str_Setting_Info, after the ten tables before it
    }

    inline constexpr int kProgInputs = 64;   // BAC_INPUT_ITEM_COUNT

    // The file's tables after its three bytes, in the order Save File writes
    // them: how many items, how long each is, and the version that added
    // the table. `count` and `item` are T3000's names for the two, as Save
    // File's loop has them, for T5000Conformance to hold them to.
    struct ProgSection
    {
        const char* count;   // "BAC_INPUT_ITEM_COUNT"
        const char* item;    // "Str_in_point", or the constant each item is
        int         items;
        size_t      size;    // of one item
        int         since;   // the version that added it
    };

    const std::vector<ProgSection>& prog_sections();

    // How long a file of `version` is, exactly: 65956 bytes for version 5,
    // 66056 for 6, 66608 for 7, 67184 for 8. 0 for any other version.
    size_t prog_file_length(int version);

    struct ProgFile
    {
        int                     version = 0;
        wire::PanelSettings     settings;
        std::vector<InputBytes> inputs;   // kProgInputs of them, as the file holds them
    };

    // Reads a .prog file's inputs and settings. Refused, with `why` saying so
    // in a sentence for the page, unless the file starts 55 FF, its version
    // is 5 to 8, and it is exactly as long as that version is. T3000 checks
    // only the first two, and reads past the end of a file cut short.
    bool read_prog_file(const uint8_t* data, size_t size, ProgFile& out, std::string& why);

    // What an import keeps of input `index` from the file's `from_file`: the
    // columns the operator sets, over the input as T3000 starts it
    // (default_input). Never what the panel sets, so C can only write what
    // an operator set:
    //   - full label, label, filter, Auto/Manual, range (with the analog or
    //     digital byte), calibration and its sign, and the signal type (the
    //     high nibble of decom), as the file has them;
    //   - the value, and the control byte a digital input's value is, only
    //     for an input in Manual. In Auto they are what the panel measured
    //     when the file was saved, which the operator did not set and T5000
    //     does not let be changed;
    //   - not the status (decom's low nibble), nor the external module's
    //     sub_id, sub_product and sub_number: the panel sets those.
    // Text is kept to its first 0 and zeros after it, as a change typed here
    // stores it; what followed the 0 is not shown, by T3000 or here.
    //
    // The operator's columns are kept as T3000 saved them, even where the
    // page's rules are stricter, so an imported input can be one the page
    // would not let be typed: a label or full label with no 0, a range the
    // Range dialog does not offer the row (PT 1K, a custom digital range, a
    // fixed row's other range), a filter or calibration on a digital input,
    // a signal type while the range is not Table 1-5, or a value in Manual
    // on a digital input past range 22. Each is what the panel held when
    // the file was saved, and refusing or changing it would lose it.
    InputBytes imported_input(int index, const InputBytes& from_file);

    // What an import keeps of the whole file, for a model whose grid shows
    // `rows` inputs: each of those that imported_input makes other than
    // default_input, by index. The file's inputs past `rows` are not kept,
    // as they could not be changed here; `past` counts those that would
    // have been.
    struct ImportedInputs
    {
        std::vector<std::pair<int, InputBytes>> inputs;
        int                                     past = 0;
    };

    ImportedInputs imported_inputs(const ProgFile& file, int rows);

    // Base64, as the page sends the file: A-Z, a-z, 0-9, + and /, padded
    // with = to a multiple of four. False for anything else, whitespace
    // included.
    bool base64_decode(const std::string& text, std::vector<uint8_t>& out);
}
