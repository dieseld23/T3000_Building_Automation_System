// Self-tests for decoding a panel's settings.
//
// Every field gets a distinct value, and every multi-byte field has four
// different bytes, so a decoder reading the right field at the wrong offset,
// or the right offset in the wrong byte order, cannot come out right.

#include "panel.h"
#include "../testing/check.h"

#include <string.h>
#include <vector>

namespace
{
    using namespace t5000::wire;
    using namespace t5000::testing;

    std::vector<uint8_t> settings_fixture()
    {
        // Filled with a byte no field below uses, so reading a neighbour of
        // the right field shows up as 0xA5.
        std::vector<uint8_t> b(kSettingsWireSize, 0xA5);

        b[settings_at::mini_type]     = 0x80 | 44;   // APM chip, T3-8AI8AO6DO
        b[settings_at::firmware_main] = 63;
        b[settings_at::firmware_sub]  = 7;
        memset(&b[settings_at::panel_name], 0, settings_at::panel_name_length);
        memcpy(&b[settings_at::panel_name], "Boiler Room", 11);
        b[settings_at::panel_number] = 12;

        // 0x0801E240 = 134341184, little-endian.
        b[settings_at::serial_number + 0] = 0x40;
        b[settings_at::serial_number + 1] = 0xE2;
        b[settings_at::serial_number + 2] = 0x01;
        b[settings_at::serial_number + 3] = 0x08;

        b[settings_at::modbus_id] = 254;

        // 0x00030D41 = 200001.
        b[settings_at::object_instance + 0] = 0x41;
        b[settings_at::object_instance + 1] = 0x0D;
        b[settings_at::object_instance + 2] = 0x03;
        b[settings_at::object_instance + 3] = 0x00;

        b[settings_at::max_var] = 128;
        b[settings_at::max_in]  = 96;
        b[settings_at::max_out] = 48;
        return b;
    }

    void test_fields()
    {
        section("a Settings block decodes field by field");

        const std::vector<uint8_t> b = settings_fixture();
        PanelSettings s;
        if (!require(decode_settings(b.data(), b.size(), s), "a 400-byte block decodes"))
            return;

        check_eq((int)s.mini_type_byte, 0x80 | 44, "mini_type as sent");
        check_eq((int)s.mini_type(), 44, "mini_type without the chip bits");
        check_eq((int)s.firmware_main, 63, "firmware main");
        check_eq((int)s.firmware_sub, 7, "firmware sub");
        check_eq(s.firmware(), 637, "firmware as T3000 compares it");
        check(memcmp(s.panel_name, "Boiler Room\0\0\0\0\0\0\0\0\0", 20) == 0, "panel name");
        check_eq((int)s.panel_number, 12, "panel number");
        check_eq((long)s.serial_number, 134341184L, "serial number, little-endian");
        check_eq((int)s.modbus_id, 254, "modbus id");
        check_eq((long)s.object_instance, 200001L, "object instance, little-endian");
        check_eq((int)s.max_var, 128, "max_var");
        check_eq((int)s.max_in, 96, "max_in");
        check_eq((int)s.max_out, 48, "max_out");
    }

    void test_chip_bits()
    {
        section("the top two bits of mini_type are the chip, not the model");

        // global_function.cpp:5251-5270: 0x40 is GD, 0x80 APM. Either way the
        // model is the low six bits - a GD-built TSTAT10 is still 9.
        std::vector<uint8_t> b = settings_fixture();
        const uint8_t chips[] = { 0x00, 0x40, 0x80, 0xC0 };
        for (uint8_t chip : chips)
        {
            b[settings_at::mini_type] = (uint8_t)(chip | 9);
            PanelSettings s;
            decode_settings(b.data(), b.size(), s);
            check_eq((int)s.mini_type(), 9, "the model survives every chip value");
        }
    }

    void test_wrong_length()
    {
        section("a Settings block of any other length is refused, as T3000 refuses it");

        const std::vector<uint8_t> b = settings_fixture();
        PanelSettings s;
        check(!decode_settings(b.data(), kSettingsWireSize - 1, s), "one byte short");
        check(!decode_settings(b.data(), 0, s), "empty");
        check(!decode_settings(nullptr, kSettingsWireSize, s), "no buffer");

        std::vector<uint8_t> longer = b;
        longer.push_back(0);
        check(!decode_settings(longer.data(), longer.size(), s), "one byte long");
    }
}

int run_panel_wire_tests()
{
    test_fields();
    test_chip_bits();
    test_wrong_length();
    return 0;
}
