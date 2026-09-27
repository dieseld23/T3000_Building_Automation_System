#pragma once

// A .prog file, as T3000's Save File writes it (SaveBacnetBinaryFile,
// global_function.cpp:12724), read for the one part T5000 imports: a panel's
// inputs, and the settings that say which panel they are. And written, for a
// device configured offline, with T3000's defaults for every table T5000
// does not keep.
//
// Nothing here reads or writes a file on disk, or sends anything to a
// device. It reads bytes the page was given, says what an import would keep,
// and makes the bytes of an export for the page to save.
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

    // prog_sections' tables, by index.
    namespace prog_table
    {
        inline constexpr size_t inputs         = 0;
        inline constexpr size_t outputs        = 1;
        inline constexpr size_t variables      = 2;
        inline constexpr size_t programs       = 3;
        inline constexpr size_t settings       = 10;
        inline constexpr size_t schedule_flags = 19;
    }

    // Where table `index` of prog_sections starts, in every version that has
    // it: each version adds its table at the end, so none before it moves.
    size_t prog_table_at(size_t index);

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
    std::string base64_encode(const std::vector<uint8_t>& data);

    // T3000's UART_ codes (global_define.h, UART_1200 = 0 up to
    // UART_921600): the code for 115200 baud, which it gives a new virtual
    // device's ports 0 and 2, and the rate each code stands for, or 0 for a
    // code it has none for.
    inline constexpr uint8_t kUart115200 = 9;
    long uart_rate(uint8_t code);

    // What T5000 holds for the device an export is for.
    struct ProgExport
    {
        uint32_t                serial    = 0;
        uint8_t                 mini_type = 0;   // the model's panel type
        std::vector<InputBytes> inputs;          // kProgInputs, as configured
    };

    // A .prog file for a device configured offline (the owner's decision,
    // 2026-09-27: what T5000 has, with T3000's defaults for the rest). Version
    // 8, prog_file_length(8) bytes. Of its tables, T5000 keeps the inputs
    // only; every other one is as T3000's Add virtual device saves it for a
    // new panel (BacnetAddVirtualDevice.cpp:201-225: ClearBacnetData, then
    // Initial_All_Point, then the settings, then SaveBacnetBinaryFile):
    //   - outputs OUT1 to OUT64, their hand switches at Auto
    //     (hw_switch_status 1); variables VAR1 to VAR128; programs PRG1 to
    //     PRG16, with no code; every other byte of them 0
    //     (Initial_All_Point, global_function.cpp:17721-17758);
    //   - the schedules' time flags all 0xFF (:17791-17793);
    //   - every other table 0. ClearBacnetData zeroes the ones
    //     Initial_All_Point leaves as they were: the graphic labels, the
    //     range tables, the variable units, the holidays' codes and the
    //     program code (:13422-13526);
    //   - the settings 0 (:17908) but for the serial, the panel type, and
    //     Initial_Virtual_Device_Setting's (:17621-17630): ports 0 and 2 at
    //     115200 baud, IP 192.168.0.3, Modbus TCP port 502. Not the object
    //     instance, Modbus id or name T3000's dialog gives a virtual device:
    //     T5000 has none of them, and Load File keeps a panel's own.
    std::vector<uint8_t> write_prog_file(const ProgExport& device);

    // What T3000's Load File would do with `file` on a panel of `model`,
    // for the page to show before an export: read back from the file's
    // bytes, not restated, so the page says what the file holds. Load File
    // writes every table, and every setting but the ones it keeps
    // (global_function.cpp:11261-11272): the serial, the object instance,
    // the panel number, the Modbus id, the IP address, subnet, gateway and
    // MAC, and the name.
    std::string describe_prog_export(const std::vector<uint8_t>& file, const std::string& model);
}
