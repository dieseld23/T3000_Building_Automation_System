// Tests for read-path selection.
//
// The case that motivated this whole module is a TSTAT10 one firmware revision
// below the PTP cutoff. It is the combination most likely to appear in the field
// and least likely to be on a desk, so it is pinned here rather than discovered
// later.

#include "read_path.h"
#include "../testing/check.h"
#include "../../T3000/ProductModel.h"

#include <string.h>

namespace
{
    using namespace t5000::device;
    using namespace t5000::testing;

    // From T3000/global_define.h.
    constexpr int kModbusRs485      = 0;
    constexpr int kModbusTcpip      = 1;
    constexpr int kBacnetIp         = 3;
    constexpr int kMbTcpipToMbRs485 = 13;
    constexpr int kThirdPartyBacBip = 253;

    void test_refused_protocol_set_is_exact()
    {
        section("the refused-protocol set matches the guard exactly");

        check(is_refused_by_private_data(kModbusRs485), "MODBUS_RS485 is refused");
        check(is_refused_by_private_data(kMbTcpipToMbRs485), "MB_TCPIP_TO_MB_RS485 is refused");
        check(is_refused_by_private_data(kThirdPartyBacBip), "THIRD_PARTY_BAC_BIP is refused");

        // The guard names three protocols. MODBUS_TCPIP is Modbus by name but is
        // NOT in it, which is exactly the sort of thing a reasonable person
        // assumes their way past.
        check(!is_refused_by_private_data(kModbusTcpip), "MODBUS_TCPIP is NOT refused");
        check(!is_refused_by_private_data(kBacnetIp), "BACnet/IP is not refused");
    }

    void test_private_data_device_set()
    {
        section("private-data device set mirrors Bacnet_Private_Device");

        check(is_private_data_device(PM_TSTAT10), "TSTAT10 is a private-data device");
        check(is_private_data_device(PM_CM5), "CM5");
        check(is_private_data_device(PM_MINIPANEL), "MINIPANEL");
        check(is_private_data_device(PM_MINIPANEL_ARM), "MINIPANEL_ARM");
        check(is_private_data_device(PM_ESP32_T3_SERIES), "ESP32_T3_SERIES");
        check(!is_private_data_device(9999), "an unknown product is not");
    }

    void test_firmware_cutoff_is_inclusive()
    {
        section("PTP firmware cutoff is >= 525, not > 525");

        check(!ptp_would_be_enabled(PM_TSTAT10, 524), "524 does not enable PTP");
        check(ptp_would_be_enabled(PM_TSTAT10, 525), "525 does enable PTP");
        check(ptp_would_be_enabled(PM_TSTAT10, 526), "526 does enable PTP");

        // BacnetView.cpp:7740 enables PTP for the ESP32 series without looking
        // at the firmware version at all.
        check(ptp_would_be_enabled(PM_ESP32_T3_SERIES, 0), "ESP32 ignores firmware entirely");
    }

    void test_tstat_below_cutoff_falls_back_and_explains()
    {
        section("a Tstat below the cutoff falls back to registers, with a reason");

        Decision d = choose_read_path(PM_TSTAT10, 520, kMbTcpipToMbRs485);

        check(d.path == ReadPath::ModbusRegisters, "falls back to Modbus registers");

        // The point of the detail string is that it names the cause. An empty
        // grid with no explanation is the failure this project exists to avoid.
        check(d.detail.find("525") != std::string::npos, "detail names the required firmware");
        check(d.detail.find("520") != std::string::npos, "detail names the device's firmware");
        check(!d.detail.empty(), "detail is not empty");

        // Regression: detail was a char[192] and this exact message truncated to
        // "...would enable the faster pat". The truncation lands at the END of
        // the sentence, which is where the actionable part is, so it was invisible
        // in a glance at the output and only showed up when the page rendered it.
        check(d.detail.back() == '.', "detail ends in a full stop, not mid-word");
    }

    void test_tstat_at_cutoff_uses_ptp()
    {
        section("a current Tstat on Modbus uses the PTP tunnel");

        Decision d = choose_read_path(PM_TSTAT10, 525, kMbTcpipToMbRs485);
        check(d.path == ReadPath::PrivateDataOverPtp, "uses private data over PTP");
        check_streq(d.summary, "private data over PTP", "summary");
    }

    void test_non_modbus_transport_needs_no_ptp()
    {
        section("a non-refused transport reads private data regardless of firmware");

        // Low firmware, but BACnet/IP is not in the guard, so the tunnel is
        // irrelevant. Getting this wrong would have the tool demand a firmware
        // update from devices that never needed one.
        Decision d = choose_read_path(PM_CM5, 100, kBacnetIp);
        check(d.path == ReadPath::PrivateData, "reads private data directly");
    }

    void test_unknown_product_on_modbus()
    {
        section("an unknown product on Modbus goes to registers");

        Decision d = choose_read_path(9999, 600, kModbusRs485);
        check(d.path == ReadPath::ModbusRegisters, "falls back to registers");
        check(d.detail.find("does not support private-data") != std::string::npos,
              "detail explains it is the product, not the firmware");
    }
}

int run_read_path_tests()
{
    test_refused_protocol_set_is_exact();
    test_private_data_device_set();
    test_firmware_cutoff_is_inclusive();
    test_tstat_below_cutoff_falls_back_and_explains();
    test_tstat_at_cutoff_uses_ptp();
    test_non_modbus_transport_needs_no_ptp();
    test_unknown_product_on_modbus();
    return 0;
}
