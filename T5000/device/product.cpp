#include "product.h"

#include <stddef.h>

namespace t5000::device
{
    namespace
    {
        // Point counts, transcribed from T3000/global_define.h:1340-1426.
        //
        // These are keyed by MiniType because that is what T3000 selects on -
        // see the if-chains at BacnetInput.cpp:158-312 and
        // BacnetOutput.cpp:421-550, which test `bacnet_device_type`, itself
        // assigned from Device_Basic_Setting.reg.mini_type.
        //
        // A product absent from this table gets known=false rather than zeros.
        // The difference matters: T3_BMS genuinely has 0/0/0/0 by design
        // (global_define.h:1402-1405, a comms-only device), whereas an
        // unlisted type means we do not know. Collapsing those two into "0
        // points" would make an unimplemented product look like a valid empty
        // one and show the operator an empty list instead of an error.
        struct CountRow
        {
            MiniType type;
            int ia, id, oa, od;
        };

        constexpr CountRow kCounts[] = {
            // MiniType             IN_A IN_D OUT_A OUT_D    source constant prefix
            { MiniType::Cm5,            10,  8,   0,   10 },  // CM5_MINIPANEL_*
            { MiniType::BigMiniPanel,   32,  0,  12,   12 },  // BIG_MINIPANEL_*
            { MiniType::SmallMiniPanel, 16,  0,   4,    6 },  // SMALL_MINIPANEL_*
            { MiniType::TinyMiniPanel,  11,  0,   2,    6 },  // TINY_MINIPANEL_*
            { MiniType::TinyExMiniPanel, 8,  0,   6,    8 },  // TINYEX_MINIPANEL_*

            // The ARM panels reuse the original panels' counts - BacnetInput.cpp
            // tests them with || against the same constants (e.g. :158 pairs
            // BIG_MINIPANEL with MINIPANELARM). Same shape, different silicon.
            { MiniType::MiniPanelArm,   32,  0,  12,   12 },
            { MiniType::MiniPanelArmLb, 16,  0,   4,    6 },
            { MiniType::MiniPanelArmTb,  8,  0,   6,    8 },

            { MiniType::Tb11I,          11,  0,   5,    6 },  // TB_11I_IN_*, T3_TB_11I_OUT_*
            { MiniType::Bms,             0,  0,   0,    0 },  // T3_BMS_* - deliberate
            { MiniType::FanModule,      12,  0,   1,    0 },  // FAN_MODULE_IN_*, FAN_MOUDLE_OUT_*
            { MiniType::EspRmc,         18,  0,   0,    7 },  // RMC_*
            { MiniType::Rmc1232,        32,  0,   0,    4 },  // RMC1232_*
            { MiniType::Ng3,            24,  0,   4,    8 },  // NG3_*
            { MiniType::ThreeIic,       10,  0,   3,    0 },  // T3_3IIC_*
            { MiniType::EspLw,           0,  0,   6,    0 },  // T3_ESP_LW_*
            { MiniType::T38AI8AO6DO,     0,  0,   8,    6 },  // T38AI8AO6DO_OUT_* (no IN_* defined)
            { MiniType::T322AI,          0,  0,   0,    0 },  // T322AI_OUT_* - analog-in module
            { MiniType::T332AI,          0,  0,   0,    0 },  // T332AI_OUT_*
        };

        // Capability table, keyed by what the HARDWARE reports.
        //
        // Data paths are solid: the five BacnetPrivateData entries are exactly
        // the set Bacnet_Private_Device() returns true for
        // (global_function.cpp:13544-13557), which is also the set
        // read_path.cpp already encodes.
        //
        // Screen sets are LESS certain than data paths. They come from the
        // product-conditional tab logic in T3000View.cpp/MainFrm.cpp, which was
        // surveyed rather than exhaustively verified. Treat a screen set as a
        // starting point to confirm against a real device, not as established
        // fact - which is what SupportState is for.
        constexpr Screen kFullController =
            Screen::Inputs | Screen::Outputs | Screen::Variables | Screen::Programs |
            Screen::Controllers | Screen::Weekly | Screen::Annual | Screen::Monitor |
            Screen::AlarmLog | Screen::Settings | Screen::UserLogin |
            Screen::RemotePoint | Screen::Array | Screen::Pvar;

        // Thermostats read through product_register_value[] rather than the
        // point structs, and have no Programs or Controller screens.
        constexpr Screen kThermostat =
            Screen::Inputs | Screen::Outputs | Screen::Tstat |
            Screen::Weekly | Screen::Annual | Screen::Settings;

        // I/O and sensor modules: points and configuration, nothing else.
        constexpr Screen kIoModule =
            Screen::Inputs | Screen::Outputs | Screen::Settings | Screen::RemotePoint;

        constexpr Capabilities kProducts[] = {
            // --- The five private-data devices. ------------------------------
            // Bacnet_Private_Device() at global_function.cpp:13544.
            { ProductClassId::Cm5, "CM5", DataPath::BacnetPrivateData,
              kFullController, SupportState::Implemented, nullptr },
            { ProductClassId::MiniPanel, "MiniPanel", DataPath::BacnetPrivateData,
              kFullController, SupportState::Implemented, nullptr },
            { ProductClassId::MiniPanelArm, "MiniPanel ARM", DataPath::BacnetPrivateData,
              kFullController, SupportState::Implemented, nullptr },
            { ProductClassId::Esp32T3Series, "T3 Series (ESP32)", DataPath::BacnetPrivateData,
              kFullController, SupportState::Implemented,
              "gets the PTP tunnel unconditionally, without the >=525 firmware "
              "check other private-data devices need (BacnetView.cpp:7736)" },
            { ProductClassId::Tstat10, "TSTAT10", DataPath::BacnetPrivateData,
              kFullController, SupportState::Implemented,
              "a thermostat that is a private-data device - it has T3 program "
              "support, so it takes the controller path, not the Tstat path" },

            // --- Thermostats on the register path. ---------------------------
            { ProductClassId::Tstat6, "TSTAT6", DataPath::TstatRegisters,
              kThermostat, SupportState::Unverified, nullptr },
            { ProductClassId::Tstat7, "TSTAT7", DataPath::TstatRegisters,
              kThermostat, SupportState::Unverified, nullptr },
            { ProductClassId::Tstat8, "TSTAT8", DataPath::TstatRegisters,
              kThermostat, SupportState::Unverified, nullptr },
            { ProductClassId::Tstat9, "TSTAT9", DataPath::TstatRegisters,
              kThermostat, SupportState::Unverified, nullptr },
            { ProductClassId::Tstat8Wifi, "TSTAT8 WiFi", DataPath::TstatRegisters,
              kThermostat, SupportState::Unverified, nullptr },
            { ProductClassId::Tstat8Occ, "TSTAT8 OCC", DataPath::TstatRegisters,
              kThermostat, SupportState::Unverified, nullptr },
            { ProductClassId::Tstat8_220V, "TSTAT8 220V", DataPath::TstatRegisters,
              kThermostat, SupportState::Unverified, nullptr },
            { ProductClassId::Tstat8Program, "TSTAT8 Programmable", DataPath::TstatRegisters,
              kThermostat, SupportState::Unverified, nullptr },

            // --- Modbus modules. ---------------------------------------------
            { ProductClassId::AirlabEsp32, "AirLab (ESP32)", DataPath::ModbusRegisters,
              kIoModule, SupportState::Unverified,
              "Modbus only - cannot use GetPrivateData_Blocking, which refuses "
              "Modbus transports without the PTP tunnel" },
            { ProductClassId::FanModule, "Fan Module", DataPath::ModbusRegisters,
              kIoModule, SupportState::Unverified,
              "ProductModel.h:97 notes it had no interface in T3000 yet" },
            { ProductClassId::T38AI8AO6DO, "T38AI8AO6DO", DataPath::ModbusRegisters,
              kIoModule, SupportState::Unverified, nullptr },
            { ProductClassId::T322AI, "T322AI", DataPath::ModbusRegisters,
              kIoModule, SupportState::Unverified, nullptr },
            { ProductClassId::T332AIArm, "T332AI (ARM)", DataPath::ModbusRegisters,
              kIoModule, SupportState::Unverified, nullptr },
            { ProductClassId::T3PT12, "T3PT12", DataPath::ModbusRegisters,
              kIoModule, SupportState::Unverified, nullptr },
            { ProductClassId::T36CTA, "T36CTA", DataPath::ModbusRegisters,
              kIoModule, SupportState::Unverified, nullptr },

            // --- Explicitly not us. ------------------------------------------
            { ProductClassId::ThirdPartyDevice, "Third-party device", DataPath::Unsupported,
              Screen::None, SupportState::NotImplemented,
              "PROTOCOL_THIRD_PARTY_BAC_BIP is one of the three transports "
              "GetPrivateData_Blocking refuses outright" },
        };

        // T3000's Add virtual device list, init_product_list at
        // global_function.cpp:11980-12199, in its order. Each entry's pid is
        // the product and its sub_pid the panel type; the line is its
        // cs_name's. The names are Getminitypename's
        // (BacnetSetting.cpp:148-218), which is what T3000's Settings page
        // shows for a panel. The list spells some differently: Tstat10,
        // T3_OEM_12I, T3-RMC1216, T3-RMC1232, T3-NG2-TYPE2, T3_3IIC.
        //
        // Not here:
        //   - the list's "Custom Device" (pid 254, :11984), a third-party
        //     device, which cannot be added by hand; add_device says why;
        //   - T3-TB-11I and the Asix MiniPanels' types, which Getminitypename
        //     names but the list does not pair with a product.
        constexpr Model kModels[] = {
            { "T3-BB",         ProductClassId::MiniPanelArm,  MiniType::MiniPanelArm },   // :11995
            { "T3-LB",         ProductClassId::MiniPanelArm,  MiniType::MiniPanelArmLb }, // :12006
            { "T3-TB",         ProductClassId::MiniPanelArm,  MiniType::MiniPanelArmTb }, // :12017
            { "T3-Nano",       ProductClassId::MiniPanelArm,  MiniType::MiniPanelArmNb }, // :12028
            { "T3-ESP-LW",     ProductClassId::Esp32T3Series, MiniType::EspLw },          // :12039
            { "T3-FAN-MODULE", ProductClassId::MiniPanelArm,  MiniType::FanModule },      // :12050
            { "T3-RMC",        ProductClassId::Esp32T3Series, MiniType::EspRmc },         // :12061
            { "T3-RMC-1232",   ProductClassId::Esp32T3Series, MiniType::Rmc1232 },        // :12072
            { "T3-BMS",        ProductClassId::Esp32T3Series, MiniType::Bms },            // :12083
            { "T3-NG2",        ProductClassId::Esp32T3Series, MiniType::Ng3 },            // :12094

            // The list gives this one sub_pid T3_NG3 (:12113), the entry
            // above's, beside an ao_count of T3_3IIC_IN_A (:12109): a copy of
            // the entry above, finished only in part. Its panel type is
            // T3_3IIC, the one Getminitypename names T3-3IIC.
            { "T3-3IIC",       ProductClassId::Esp32T3Series, MiniType::ThreeIic },       // :12105

            { "TSTAT10",       ProductClassId::Tstat10,       MiniType::Tstat10 },        // :12116
            { "T3-OEM",        ProductClassId::Tstat10,       MiniType::Oem },            // :12127
            { "T3-OEM-12I",    ProductClassId::Tstat10,       MiniType::Oem12I },         // :12138
            { "TSTAT11",       ProductClassId::Esp32T3Series, MiniType::Tstat11 },        // :12150
        };

        // Returned for anything not in the table. Deliberately has no screens
        // and no data path: an unrecognised device must present as "not
        // supported yet", never as a default product read with a guessed
        // register map.
        constexpr Capabilities kUnknown = {
            ProductClassId::Unknown, "Unknown product", DataPath::Unsupported,
            Screen::None, SupportState::Unverified,
            "not in the capability table - identify it before reading from it"
        };
    }

    const Capabilities& capabilities(ProductClassId id)
    {
        for (const auto& p : kProducts)
            if (p.id == id)
                return p;
        return kUnknown;
    }

    bool is_supported(ProductClassId id)
    {
        const Capabilities& c = capabilities(id);
        return c.support == SupportState::Implemented ||
               c.support == SupportState::Partial ||
               c.support == SupportState::ZeroIoByDesign;
    }

    PointCounts point_counts(MiniType type)
    {
        for (const auto& r : kCounts)
            if (r.type == type)
                return PointCounts{ r.ia, r.id, r.oa, r.od, true };

        return PointCounts{ 0, 0, 0, 0, false };
    }

    MiniTypeInfo mini_type_info(MiniType type)
    {
        // Every entry below was established by grepping the whole of
        // BacnetInput.cpp and BacnetOutput.cpp for the identifier. That
        // matters: an earlier survey searched line ranges, missed hits
        // outside them, and called nine types unimplemented that are not.
        switch (type)
        {
        // --- Variants of a PM_TSTAT10, with some inputs on a fixed range. -
        // BacnetInput.cpp:1662-1683. These have no counts of their own and
        // never appear in the init chains, which is correct rather than
        // missing - they are one product wearing four labels.
        case MiniType::Oem:
            return { MiniTypeSupport::VariantOfTstat10, 13, 17,
                     "a PM_TSTAT10 whose inputs 14-18 (rows 13-17) have a fixed range" };
        case MiniType::Oem12I:
            return { MiniTypeSupport::VariantOfTstat10, 17, 21,
                     "a PM_TSTAT10 whose inputs 18-22 (rows 17-21) have a fixed range" };
        case MiniType::Tstat10:
            return { MiniTypeSupport::VariantOfTstat10, 9, 12,
                     "a PM_TSTAT10 whose inputs 10-13 (rows 9-12) have a fixed range" };
        case MiniType::Tstat11:
            return { MiniTypeSupport::VariantOfTstat10, 9, 12,
                     "a PM_TSTAT10 whose inputs 10-13 (rows 9-12) have a fixed range, as on a TSTAT10" };

        // --- One direction only. ----------------------------------------
        case MiniType::FanModule:
            return { MiniTypeSupport::InputsOnly, 0, 0,
                     "absent from BacnetOutput.cpp entirely (whole-file grep, 0 hits)" };
        case MiniType::ThreeIic:
            return { MiniTypeSupport::InputsOnly, 0, 0,
                     "output counts never set; the BacnetOutput.cpp hit at :742 "
                     "is the column-display chain, not the init chain" };
        case MiniType::T3PT12:
            return { MiniTypeSupport::InputsOnly, 0, 0,
                     "absent from BacnetOutput.cpp (whole-file grep, 0 hits)" };
        case MiniType::EspLw:
            return { MiniTypeSupport::OutputsOnly, 0, 0,
                     "absent from BacnetInput.cpp (whole-file grep, 0 hits); "
                     "a lighting controller, so inputs may be genuinely absent "
                     "rather than unfinished" };
        case MiniType::MiniPanelArmNb:
            return { MiniTypeSupport::InputsOnly, 0, 0,
                     "absent from BacnetOutput.cpp; maps to BACNET_ROUTER's "
                     "zero counts, so it may be a router rather than a panel" };

        // --- Never implemented. ------------------------------------------
        // Whole-file greps of both files return zero for all four.
        case MiniType::EspTransducer:
            return { MiniTypeSupport::NotImplemented, 0, 0, "marked TBD in global_define.h" };
        case MiniType::EspTstat9:
            return { MiniTypeSupport::NotImplemented, 0, 0, "marked TBD in global_define.h" };
        case MiniType::EspSauter:
            return { MiniTypeSupport::NotImplemented, 0, 0, "marked TBD in global_define.h" };
        case MiniType::Airlab:
            return { MiniTypeSupport::NotImplemented, 0, 0,
                     "the mini_type is unused, though the HARDWARE is handled - "
                     "PM_AIRLAB_ESP32 gets an early return at BacnetInput.cpp:1660" };

        default:
            break;
        }

        return { MiniTypeSupport::CountsFromInitChain, 0, 0, nullptr };
    }

    PanelResolution resolve_panel(ProductClassId hardware, int raw_mini_type)
    {
        const auto type = static_cast<MiniType>(raw_mini_type & 0xFF);

        if (raw_mini_type != 0)
            return { type, true, point_counts(type), "panel type read from the device" };

        // mini_type is 0. That is CM5's value AND the unset value.
        if (hardware == ProductClassId::Cm5)
        {
            return { MiniType::Cm5, true, point_counts(MiniType::Cm5),
                     "mini_type is 0, but the hardware reports CM5, for which 0 "
                     "is the correct value" };
        }

        // Anything else with mini_type 0 has not been configured, and we must
        // not borrow CM5's point counts for it.
        return { MiniType::NotSet, false, PointCounts{ 0, 0, 0, 0, false },
                 "mini_type is 0 and the hardware is not a CM5, so the panel "
                 "type is unset - point counts are unknown, not zero" };
    }

    CapabilityTable known_products()
    {
        return CapabilityTable{ kProducts, (int)(sizeof(kProducts) / sizeof(kProducts[0])) };
    }

    const char* to_string(ProductClassId id)
    {
        return capabilities(id).name;
    }

    const char* to_string(DataPath path)
    {
        switch (path)
        {
        case DataPath::BacnetPrivateData: return "BACnet private data";
        case DataPath::ModbusRegisters:   return "Modbus registers";
        case DataPath::TstatRegisters:    return "thermostat registers";
        case DataPath::Unsupported:       return "unsupported";
        }
        return "unsupported";
    }

    const char* to_string(SupportState state)
    {
        switch (state)
        {
        case SupportState::Implemented:    return "implemented";
        case SupportState::Partial:        return "partially implemented";
        case SupportState::ZeroIoByDesign: return "no points by design";
        case SupportState::NotImplemented: return "not implemented";
        case SupportState::Unverified:     return "not yet verified";
        }
        return "not yet verified";
    }

    const char* to_string(MiniType type)
    {
        switch (type)
        {
        // NotSet and Cm5 are both 0; see the note in product.h. The case is
        // written once and reports the ambiguity rather than picking one.
        case MiniType::NotSet:          return "CM5 or unconfigured (mini_type 0)";
        case MiniType::BigMiniPanel:    return "Big MiniPanel";
        case MiniType::SmallMiniPanel:  return "Small MiniPanel";
        case MiniType::TinyMiniPanel:   return "Tiny MiniPanel";
        case MiniType::TinyExMiniPanel: return "Tiny EX MiniPanel";
        case MiniType::MiniPanelArm:    return "MiniPanel ARM";
        case MiniType::MiniPanelArmLb:  return "MiniPanel ARM LB";
        case MiniType::MiniPanelArmTb:  return "MiniPanel ARM TB";
        case MiniType::MiniPanelArmNb:  return "MiniPanel ARM NB";
        case MiniType::Tstat10:         return "TSTAT10";
        case MiniType::Bms:             return "BMS";
        case MiniType::Oem:             return "OEM";
        case MiniType::Tb11I:           return "TB-11I";
        case MiniType::FanModule:       return "Fan Module";
        case MiniType::Oem12I:          return "OEM-12I";
        case MiniType::Airlab:          return "AirLab";
        case MiniType::EspTransducer:   return "ESP Transducer";
        case MiniType::EspTstat9:       return "ESP TSTAT9";
        case MiniType::EspSauter:       return "ESP Sauter";
        case MiniType::EspRmc:          return "ESP RMC";
        case MiniType::EspLw:           return "ESP Lighting";
        case MiniType::Ng3:             return "NG3";
        case MiniType::ThreeIic:        return "3IIC";
        case MiniType::Tstat11:         return "TSTAT11";
        case MiniType::Rmc1232:         return "RMC1232";
        case MiniType::T322AI:          return "T322AI";
        case MiniType::T38AI8AO6DO:     return "T38AI8AO6DO";
        case MiniType::T3PT12:          return "T3PT12";
        case MiniType::T332AI:          return "T332AI";
        case MiniType::T36CTA:          return "T36CTA";
        }
        return "unrecognised panel type";
    }

    ModelTable known_models()
    {
        return ModelTable{ kModels, (int)(sizeof(kModels) / sizeof(kModels[0])) };
    }

    const Model* find_model(ProductClassId product, int raw_mini_type)
    {
        // Compared whole, not cut to a byte, so 267 is not panel type 11.
        // Nothing matches 0, which no model has: it is CM5's panel type, and
        // "not set" on every other product (resolve_panel).
        for (const auto& m : kModels)
            if (m.product == product && static_cast<int>(m.type) == raw_mini_type)
                return &m;
        return nullptr;
    }

    const char* panel_name(ProductClassId product, MiniType type)
    {
        if (const Model* m = find_model(product, static_cast<int>(type)))
            return m->name;

        // Panel type 0 on CM5 hardware is a CM5 and nothing else
        // (resolve_panel), and T3000 names it so (Getminitypename,
        // BacnetSetting.cpp:152-153). to_string cannot: it is given the
        // type alone, and 0 on anything else is "not set".
        if (product == ProductClassId::Cm5 && type == MiniType::Cm5)
            return "CM5";
        return to_string(type);
    }

    PanelResolution resolve_chosen_panel(ProductClassId product, int raw_mini_type)
    {
        PanelResolution r = resolve_panel(product, raw_mini_type);
        r.reason = r.resolved
                       ? "chosen when this entry was added by hand - no device has been read"
                       : "no model was chosen when this entry was added by hand, and no device has "
                         "been read";
        return r;
    }
}
