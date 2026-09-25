#include "panel.h"

#include <string.h>

namespace t5000::wire
{
    namespace
    {
        // Little-endian, as T3000 reads these fields (global_function.cpp:5202,
        // :5205-5207) - the struct is copied raw on an x86 controller.
        uint32_t le32(const uint8_t* p)
        {
            return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) |
                   ((uint32_t)p[1] << 8) | (uint32_t)p[0];
        }
    }

    bool decode_settings(const uint8_t* buffer, size_t length, PanelSettings& out)
    {
        if (buffer == nullptr || length != kSettingsWireSize)
            return false;

        out.mini_type_byte = buffer[settings_at::mini_type];
        out.firmware_main  = buffer[settings_at::firmware_main];
        out.firmware_sub   = buffer[settings_at::firmware_sub];
        memcpy(out.panel_name, buffer + settings_at::panel_name, settings_at::panel_name_length);
        out.panel_number    = buffer[settings_at::panel_number];
        out.serial_number   = le32(buffer + settings_at::serial_number);
        out.modbus_id       = buffer[settings_at::modbus_id];
        out.object_instance = le32(buffer + settings_at::object_instance);
        out.max_var         = buffer[settings_at::max_var];
        out.max_in          = buffer[settings_at::max_in];
        out.max_out         = buffer[settings_at::max_out];
        return true;
    }
}
