// Proves that the offsets in panel.h are where CM5/ud_str.h puts those
// fields. Nothing here runs: every check is a static_assert.
//
// The struct is one of two witnesses. The other is the handler that decodes
// a Settings reply field by field (global_function.cpp:5123-5243), whose walk
// was checked against these offsets by hand: mini_type 19, then debug, then
// pro_info from 21, the ten unused bytes, the com and baud fields, panel_type
// 51, panel_name 52-71, panel_number 73, the three 32-byte DynDNS strings,
// n_serial_number 177, the 10-byte UN_Time, modbus_id 197, object_instance
// 198, and max_var/max_in/max_out 266-268. The struct can be asserted; the
// walk cannot, so if a field is ever added to one and not the other, this
// file catches only half of it.

#include <stddef.h>
#include <stdint.h>

#include "cm5_header.h"
#include "panel.h"

namespace
{
    namespace w = t5000::wire;

    static_assert(sizeof(::Str_Setting_Info) == w::kSettingsWireSize,
        "Str_Setting_Info is no longer 400 bytes; T3000 rejects any other length");

#define SETTINGS_FIELD_AT(field, ours)                                         \
    static_assert(offsetof(::Str_Setting_Info, reg.field) == (ours),           \
        "Str_Setting_Info::reg." #field " is not at the offset panel.h reads")

    SETTINGS_FIELD_AT(mini_type, w::settings_at::mini_type);
    SETTINGS_FIELD_AT(pro_info.firmware0_rev_main, w::settings_at::firmware_main);
    SETTINGS_FIELD_AT(pro_info.firmware0_rev_sub, w::settings_at::firmware_sub);
    SETTINGS_FIELD_AT(panel_name, w::settings_at::panel_name);
    SETTINGS_FIELD_AT(panel_number, w::settings_at::panel_number);
    SETTINGS_FIELD_AT(n_serial_number, w::settings_at::serial_number);
    SETTINGS_FIELD_AT(modbus_id, w::settings_at::modbus_id);
    SETTINGS_FIELD_AT(object_instance, w::settings_at::object_instance);
    SETTINGS_FIELD_AT(max_var, w::settings_at::max_var);
    SETTINGS_FIELD_AT(max_in, w::settings_at::max_in);
    SETTINGS_FIELD_AT(max_out, w::settings_at::max_out);
#undef SETTINGS_FIELD_AT

    // And the sizes that decide how many bytes are read at each.
    static_assert(sizeof(::Str_Setting_Info{}.reg.panel_name) == w::settings_at::panel_name_length,
        "panel_name changed size");
    static_assert(sizeof(::Str_Setting_Info{}.reg.n_serial_number) == 4, "n_serial_number is not 4 bytes");
    static_assert(sizeof(::Str_Setting_Info{}.reg.object_instance) == 4, "object_instance is not 4 bytes");

    // The custom digital ranges.
    static_assert(sizeof(::Str_Units_element) == w::kCustomUnitWireSize, "Str_Units_element is not 25 bytes");
    static_assert(offsetof(::Str_Units_element, direct) == w::custom_unit_at::direct, "direct moved");
    static_assert(offsetof(::Str_Units_element, digital_units_off) == w::custom_unit_at::off, "digital_units_off moved");
    static_assert(offsetof(::Str_Units_element, digital_units_on) == w::custom_unit_at::on, "digital_units_on moved");
    static_assert(sizeof(::Str_Units_element{}.digital_units_off) == w::custom_unit_at::name_length, "off name changed size");
    static_assert(sizeof(::Str_Units_element{}.digital_units_on) == w::custom_unit_at::name_length, "on name changed size");

    // The custom analog tables.
    static_assert(sizeof(::Str_table_point) == w::kAnalogTableWireSize, "Str_table_point is not 105 bytes");
    static_assert(offsetof(::Str_table_point, table_name) == w::analog_table_at::name, "table_name moved");
    static_assert(sizeof(::Str_table_point{}.table_name) == w::analog_table_at::name_length, "table_name changed size");
}
