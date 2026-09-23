#pragma once

// Product identity, as two separate things that T3000 keeps in one variable.
//
// This is the single most important type in the device layer, because getting
// it wrong means reading the wrong registers on live building equipment.
//
// T3000 has TWO product numbering schemes:
//
//   ProductClassId  (PM_* in T3000/ProductModel.h, ~90 entries)
//       what the HARDWARE reports about itself. Determines the protocol and
//       which data path can be used.
//
//   MiniType        (T3_*/PRODUCT_CM5/BIG_MINIPANEL in global_define.h:1305,
//                    30 entries)
//       what a PANEL IS CONFIGURED AS. Stored on the device in
//       Device_Basic_Setting.reg.mini_type. Determines how many points it has.
//
// They meet in exactly one place, BacnetInput.cpp:1305-1306:
//
//     if ((Bacnet_Private_Device(selected_product_Node.product_class_id)) &&
//          Device_Basic_Setting.reg.mini_type != 0)
//         bacnet_device_type = Device_Basic_Setting.reg.mini_type;
//
// ...and from there `bacnet_device_type` - a plain int - is compared against
// constants from BOTH schemes in the same if-chain (BacnetOutput.cpp:421-550
// tests BIG_MINIPANEL and T3_ESP_LW alongside PM_T38AI8AO6DO and PM_T322AI).
//
// That is safe today only by coincidence of numbering. The high T3_ values
// were chosen to EQUAL their PM_ counterparts:
//
//     PID_T322AI  == PM_T322AI       (43)     T38AI8AO6DO == PM_T38AI8AO6DO (44)
//     PID_T3PT12  == PM_T3PT12       (46)     PID_T332AI  == PM_T332AI_ARM  (53)
//     PID_T36CTA  == PM_T36CTA       (95)
//
// ...which is why the enum has gaps. But the LOW values collide with entirely
// unrelated products:
//
//     T3_BMS(10)     vs PM_TSTAT10(10)        T3_TSTAT10(9)  vs PM_TSTAT8(9)
//     T3_ESP_LW(21)  vs PM_T3IOA(21)          T3_NG3(22)     vs PM_T332AI(22)
//     T3_3IIC(26)    vs PM_T3PT10(26)         T3_TSTAT11(27) vs PM_T3PERFORMANCE(27)
//     T3_RMC1232(29) vs PM_T36CT(29)
//
// Those cases are unreachable in the shipping app because a private-data
// panel's mini_type is never a thermostat model - an invariant held by
// convention and nothing else.
//
// So: these are `enum class`, not int. A scoped enum will not implicitly
// convert to another scoped enum or to an integer, which makes the mix-up a
// compile error rather than a wrong register address. product_selftest.cpp
// asserts the collisions still exist numerically, so that if someone ever
// "helpfully" renumbers one scheme, the note above stops being a lie.

#include <stdint.h>

namespace t5000::device
{
    // What the hardware reports as product_class_id. From T3000/ProductModel.h.
    // The full list is here so that no real device is unrepresentable; the
    // capability table below is populated for the ones we actually handle.
    enum class ProductClassId : uint8_t
    {
        Unknown = 0,

        Tstat5B = 1, Tstat5A = 2, Tstat5B2 = 3, Tstat5C = 4,
        Tstat6 = 6, Tstat7 = 7, Tstat5i = 8, Tstat8 = 9,
        Tstat10 = 10,                  // Wifi Tstat with T3 program support
        Tstat5D = 12,
        AirQuality = 13, HumTempSensor = 14, TstatRunAr = 15,
        Tstat5E = 16, Tstat5F = 17, Tstat5G = 18, Tstat5H = 19,
        T38I13O = 20, T3IOA = 21, T332AI = 22, T38AI16O = 23,
        Zigbee = 24, FlexDriver = 25, T3PT10 = 26, T3Performance = 27,
        T34AO = 28, T36CT = 29, Solar = 30, FwmTransducer = 31,
        Co2Net = 32, Co2Rs485 = 33, Co2Node = 34,
        MiniPanel = 35,
        CsSmAc = 36, CsSmDc = 37, CsRsmAc = 38, CsRsmDc = 39,
        Pressure = 40, Pm5E = 41, HumR = 42,
        T322AI = 43, T38AI8AO6DO = 44, PressureSensor = 45, T3PT12 = 46,
        T322AIVG = 47, T38IOVG = 48, T3PTVG = 49,
        Cm5 = 50,
        Pm5EArm = 51, Stm32Pm25 = 52, T332AIArm = 53,
        Tstat9 = 59, MultiSensor = 60, TstatAq = 62, ZigbeeRepeater = 63,
        Tstat6HumChamber = 64, AirlabEsp32 = 65,
        Beeny = 70, WaterSensor = 71, T3Lc = 72, PowerMeter = 73,
        MiniPanelArm = 74, WeatherStation = 75,
        Esp32T38AI8AO6DO = 87,
        Esp32T3Series = 88,            // the ESP32 T3 BB/LB/TB/Nano family
        Esp32T322AI = 89,
        PwmTemperatureTransducer = 90,
        Tstat8Wifi = 91, Tstat8Occ = 92, Tstat7Arm = 93, Tstat8_220V = 94,
        T36CTA = 95, Afs = 96, FanModule = 97,
        Nc = 100, Tstat8Program = 101, PwmTransducer = 104,
        LightingController = 120, BtuMeter = 121, BoatMonitor = 122,
        MiniTop = 200, Labbats = 201, TesterJig = 202,
        Stm32Co2Net = 210, Stm32Co2Rs485 = 211,
        Stm32HumNet = 212, Stm32HumRs485 = 213,
        Stm32PressureNet = 214, Stm32PressureRs485 = 215,
        Stm32Co2Node = 216,
        ThirdPartyDevice = 254,
    };

    // What a panel is configured as. From T3000/global_define.h:1305-1338,
    // read off the device as Device_Basic_Setting.reg.mini_type.
    enum class MiniType : uint8_t
    {
        // 0 is "not set". BacnetInput.cpp:1305 explicitly tests mini_type != 0
        // before trusting it, so it is a real state and not just a default.
        NotSet = 0,

        Cm5 = 0,                       // NOTE: PRODUCT_CM5 really is 0. See below.

        BigMiniPanel = 1, SmallMiniPanel = 2, TinyMiniPanel = 3,
        TinyExMiniPanel = 4,
        MiniPanelArm = 5, MiniPanelArmLb = 6, MiniPanelArmTb = 7,
        MiniPanelArmNb = 8,
        Tstat10 = 9,
        Bms = 10, Oem = 11, Tb11I = 12, FanModule = 13, Oem12I = 14,
        Airlab = 15,
        EspTransducer = 16, EspTstat9 = 17, EspSauter = 18, EspRmc = 19,
        EspLw = 21, Ng3 = 22, ThreeIic = 26, Tstat11 = 27, Rmc1232 = 29,
        T322AI = 43, T38AI8AO6DO = 44, T3PT12 = 46, T332AI = 53, T36CTA = 95,
    };

    // PRODUCT_CM5 == 0 == "not set", which means mini_type alone cannot
    // distinguish "this is a CM5" from "nobody configured this panel".
    // BacnetInput.cpp:1305 resolves it by refusing to use mini_type at all
    // when it is 0, falling back to product_class_id. Anything in T5000 that
    // reads mini_type must do the same; this assert is here so the collision
    // cannot be forgotten.
    static_assert(static_cast<uint8_t>(MiniType::Cm5) == static_cast<uint8_t>(MiniType::NotSet),
        "PRODUCT_CM5 is 0 and so is 'not set' - if this ever stops being true, "
        "the fallback logic that depends on it can be simplified");

    // How a product's data is actually read. This is the axis that matters for
    // the wire layer, and there are three of them - not one per product.
    enum class DataPath
    {
        // GetPrivateData_Blocking. Reads Str_in_point/Str_out_point directly.
        BacnetPrivateData,

        // Same structs, but fetched over Modbus at product-specific register
        // offsets resolved by name through _P() (T3000RegAddress.cpp:42).
        ModbusRegisters,

        // A separate data model entirely: CTStatInputView reads
        // product_register_value[] and never touches the point structs.
        TstatRegisters,

        Unsupported,
    };

    // The 15 in-scope screens. WINDOW_SCREEN (the graphics editor) is
    // deliberately absent - excluded from T5000 by decision, not an oversight.
    enum class Screen : uint32_t
    {
        None        = 0,
        Inputs      = 1u << 0,
        Outputs     = 1u << 1,
        Variables   = 1u << 2,
        Programs    = 1u << 3,
        Controllers = 1u << 4,
        Weekly      = 1u << 5,
        Annual      = 1u << 6,
        Monitor     = 1u << 7,
        AlarmLog    = 1u << 8,
        Tstat       = 1u << 9,
        Settings    = 1u << 10,
        UserLogin   = 1u << 11,
        RemotePoint = 1u << 12,
        Array       = 1u << 13,
        Pvar        = 1u << 14,
    };

    constexpr Screen operator|(Screen a, Screen b)
    {
        return static_cast<Screen>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    constexpr bool has(Screen set, Screen one)
    {
        return (static_cast<uint32_t>(set) & static_cast<uint32_t>(one)) != 0;
    }

    // How much of this product the SHIPPING app actually implements. Porting
    // can only copy behaviour that exists; anything below Implemented is new
    // product development and should be a deliberate decision, not a surprise.
    enum class SupportState
    {
        Implemented,      // T3000 handles it fully; we can port it
        Partial,          // T3000 handles some of it - details in the note
        ZeroIoByDesign,   // deliberately has no points (e.g. a comms-only BMS)
        NotImplemented,   // enum entry only; nothing to port
        Unverified,       // not yet checked against the source
    };

    struct Capabilities
    {
        ProductClassId id;
        const char*    name;
        DataPath       path;
        Screen         screens;
        SupportState   support;
        const char*    note;          // why, when it is not obvious. May be null.
    };

    // Looks up a product. Never returns null: an unknown product gets an entry
    // with SupportState::Unverified and no screens, so an unrecognised device
    // shows as "not supported yet" rather than being silently treated as a
    // default product and read with the wrong register map.
    const Capabilities& capabilities(ProductClassId id);

    // True when the product is one the tool can currently do anything with.
    bool is_supported(ProductClassId id);

    const char* to_string(ProductClassId id);
    const char* to_string(MiniType type);
    const char* to_string(DataPath path);
    const char* to_string(SupportState state);

    // Everything the table knows about, for the UI's product list and for
    // tests that need to walk every entry.
    struct CapabilityTable
    {
        const Capabilities* entries;
        int                 count;
    };
    CapabilityTable known_products();

    // Point counts live on the OTHER axis.
    //
    // How many points a device has is decided by what the panel is configured
    // as (mini_type), not by what the hardware is (product_class_id) - which
    // is why this is a separate lookup rather than a field on Capabilities.
    // T3000 gets this from the *_IN_A / *_IN_D / *_OUT_A / *_OUT_D constants
    // at global_define.h:1340-1410, selected by the if-chains at
    // BacnetInput.cpp:158-312 and BacnetOutput.cpp:421-550.
    //
    // Keeping the two axes in separate functions is the point: it makes it
    // impossible to write the bug where a hardware id is used to size a
    // point list.
    struct PointCounts
    {
        int  analog_inputs;
        int  digital_inputs;
        int  analog_outputs;
        int  digital_outputs;
        bool known;          // false when T3000 has no counts for this type

        int inputs()  const { return analog_inputs + digital_inputs; }
        int outputs() const { return analog_outputs + digital_outputs; }
    };

    PointCounts point_counts(MiniType type);

    // How T3000 actually implements a given panel type.
    //
    // Not every mini_type works the same way, and assuming they do is how the
    // survey of this code went wrong: it looked for each type in the
    // point-count if-chains, did not find several, and called them
    // unimplemented. Four of them are implemented a completely different way.
    enum class MiniTypeSupport
    {
        // The normal case: the type appears in the if-chains at
        // BacnetInput.cpp:158-312 / BacnetOutput.cpp:421-550 and gets its own
        // point counts.
        CountsFromInitChain,

        // Implemented as a ROW MASK over PM_TSTAT10's point list rather than
        // as a device with its own counts. BacnetInput.cpp:1662-1683 sits
        // inside `if (g_selected_product_id == PM_TSTAT10)` and hides a row
        // range per mini_type. These are variants of one product, not
        // products - which is why they have no count constants and are absent
        // from the init chains.
        RowMaskOnTstat10,

        // Counts in one direction only. The other direction falls through to
        // a default, which is a real limitation of the shipping app.
        InputsOnly,
        OutputsOnly,

        // Genuinely nowhere: whole-file greps of both BacnetInput.cpp and
        // BacnetOutput.cpp return zero. Supporting these is new product
        // development, not porting - there is no behaviour to copy.
        NotImplemented,
    };

    struct MiniTypeInfo
    {
        MiniTypeSupport support;

        // For RowMaskOnTstat10 only: the inclusive row range hidden from the
        // TSTAT10's list. Zero-zero when not applicable.
        int hidden_row_first;
        int hidden_row_last;

        const char* note;   // may be null
    };

    MiniTypeInfo mini_type_info(MiniType type);

    // Resolving the mini_type == 0 ambiguity.
    //
    // PRODUCT_CM5 is 0 and so is "nobody configured this panel", so
    // mini_type alone cannot tell them apart. T3000 resolves it by refusing
    // to use mini_type at all when it is zero (BacnetInput.cpp:1305 tests
    // `mini_type != 0` before assigning it) and falling back to what the
    // hardware reports.
    //
    // This function exists because the obvious code is wrong in a way that
    // looks right: calling point_counts(MiniType::Cm5) for an unconfigured
    // panel returns CM5's 10/8/0/10 and the UI shows eighteen confident
    // inputs for a device that never said it had any. That bug was written
    // and shipped into /api/device before this function existed.
    struct PanelResolution
    {
        MiniType    type;
        bool        resolved;    // false when mini_type is 0 and the hardware is not a CM5
        PointCounts counts;      // counts.known is false when unresolved
        const char* reason;      // always set; explains an unresolved result
    };

    PanelResolution resolve_panel(ProductClassId hardware, int raw_mini_type);
}
