#pragma once

// The panel's settings and its custom range tables, as bytes.
//
// Three blocks T3000 reads from a panel before it shows any point
// (BacnetView.cpp:5905, :6472, :6563-6565):
//
//   READ_SETTING_COMMAND        one Str_Setting_Info       400 bytes
//   READUNIT_T3000              eight Str_Units_element     25 bytes each
//   READANALOG_CUS_TABLE_T3000  five Str_table_point       105 bytes each
//
// Only the Settings fields T5000 uses are decoded, at offsets that
// conformance/panel_guard.cpp checks against CM5/ud_str.h. The custom tables are not
// decoded into structs at all: what T3000 shows from them is a name, and the
// rules it uses to cut that name out of the bytes (display/custom_ranges.cpp)
// read past a field's end, so they work on the whole reply rather than on a
// copy of one field.

#include <stddef.h>
#include <stdint.h>

namespace t5000::wire
{
    inline constexpr size_t kSettingsWireSize    = 400;
    inline constexpr size_t kCustomUnitWireSize  = 25;
    inline constexpr size_t kAnalogTableWireSize = 105;

    inline constexpr int kCustomUnitCount  = 8;   // BAC_CUSTOMER_UNITS_COUNT, global_define.h:460
    inline constexpr int kAnalogTableCount = 5;   // BAC_ALALOG_CUSTMER_RANGE_TABLE_COUNT, :466

    // Where each field T5000 reads sits in Str_Setting_Info.
    namespace settings_at
    {
        inline constexpr size_t mini_type         = 19;
        inline constexpr size_t firmware_main     = 22;    // pro_info.firmware0_rev_main
        inline constexpr size_t firmware_sub      = 23;    // pro_info.firmware0_rev_sub
        inline constexpr size_t panel_name        = 52;
        inline constexpr size_t panel_name_length = 20;
        inline constexpr size_t panel_number      = 73;
        inline constexpr size_t serial_number     = 177;   // n_serial_number, 4 bytes
        inline constexpr size_t modbus_id         = 197;
        inline constexpr size_t object_instance   = 198;   // 4 bytes
        inline constexpr size_t max_var           = 266;
        inline constexpr size_t max_in            = 267;
        inline constexpr size_t max_out           = 268;
    }

    // Str_Units_element: a direction flag, then the off and on names.
    namespace custom_unit_at
    {
        inline constexpr size_t direct      = 0;
        inline constexpr size_t off         = 1;
        inline constexpr size_t on          = 13;
        inline constexpr size_t name_length = 12;
    }

    // Str_table_point: a name, then sixteen volts/value pairs T5000 does not
    // use yet.
    namespace analog_table_at
    {
        inline constexpr size_t name        = 0;
        inline constexpr size_t name_length = 9;
    }

    struct PanelSettings
    {
        uint8_t  mini_type_byte  = 0;    // as sent; the top two bits are the chip
        uint8_t  firmware_main   = 0;
        uint8_t  firmware_sub    = 0;
        uint8_t  panel_name[settings_at::panel_name_length] = {};
        uint8_t  panel_number    = 0;
        uint32_t serial_number   = 0;
        uint8_t  modbus_id       = 0;
        uint32_t object_instance = 0;
        uint8_t  max_var         = 0;
        uint8_t  max_in          = 0;
        uint8_t  max_out         = 0;

        // What T3000 keeps as mini_type, and then as bacnet_device_type: the
        // low six bits. The top two name the processor - 0x40 GD, 0x80 APM
        // (global_function.cpp:5249-5270).
        uint8_t mini_type() const { return mini_type_byte & 0x3F; }

        // How T3000 compares firmware versions: main * 10 + sub, so 63.7 is
        // 637 (BacnetView.cpp:4964 and a dozen others).
        int firmware() const { return (int)firmware_main * 10 + (int)firmware_sub; }
    };

    // Decodes the fields above from a 400-byte Str_Setting_Info. Returns
    // false unless the buffer is exactly that long - T3000 rejects any other
    // length (global_function.cpp:5126).
    bool decode_settings(const uint8_t* buffer, size_t length, PanelSettings& out);
}
