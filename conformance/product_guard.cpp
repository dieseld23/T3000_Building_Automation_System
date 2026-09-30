// Proves that every product code in device/product.h is the one
// T3000/ProductModel.h gives that product. Nothing here runs: every check is
// a static_assert, so a renumbered product is a build failure rather than a
// device read by the wrong rules.
//
// T5000 used to include ProductModel.h wherever it compared a product code,
// so that a renumbering upstream could not leave it behind. It now uses its
// own ProductClassId everywhere, and this file is what keeps that promise:
// all ninety codes, not only the ones T5000 decides on today, since the ones
// it does not use yet are the ones nobody would think to check when it starts
// to.
//
// ProductModel.h has no includes and nothing but #defines, so it compiles
// here as it is.

#include "../device/product.h"
#include "../../T3000/ProductModel.h"

namespace
{
    using t5000::device::ProductClassId;

#define SAME_CODE(ours, theirs) \
    static_assert(static_cast<int>(ProductClassId::ours) == (theirs), #ours " is not T3000's " #theirs)

    SAME_CODE(Tstat5B, PM_TSTAT5B);
    SAME_CODE(Tstat5A, PM_TSTAT5A);
    SAME_CODE(Tstat5B2, PM_TSTAT5B2);
    SAME_CODE(Tstat5C, PM_TSTAT5C);
    SAME_CODE(Tstat6, PM_TSTAT6);
    SAME_CODE(Tstat7, PM_TSTAT7);
    SAME_CODE(Tstat5i, PM_TSTAT5i);
    SAME_CODE(Tstat8, PM_TSTAT8);
    SAME_CODE(Tstat10, PM_TSTAT10);
    SAME_CODE(Tstat5D, PM_TSTAT5D);
    SAME_CODE(AirQuality, PM_AirQuality);
    SAME_CODE(HumTempSensor, PM_HUMTEMPSENSOR);
    SAME_CODE(TstatRunAr, PM_TSTATRUNAR);
    SAME_CODE(Tstat5E, PM_TSTAT5E);
    SAME_CODE(Tstat5F, PM_TSTAT5F);
    SAME_CODE(Tstat5G, PM_TSTAT5G);
    SAME_CODE(Tstat5H, PM_TSTAT5H);
    SAME_CODE(T38I13O, PM_T38I13O);
    SAME_CODE(T3IOA, PM_T3IOA);
    SAME_CODE(T332AI, PM_T332AI);
    SAME_CODE(T38AI16O, PM_T38AI16O);
    SAME_CODE(Zigbee, PM_ZIGBEE);
    SAME_CODE(FlexDriver, PM_FLEXDRIVER);
    SAME_CODE(T3PT10, PM_T3PT10);
    SAME_CODE(T3Performance, PM_T3PERFORMANCE);
    SAME_CODE(T34AO, PM_T34AO);
    SAME_CODE(T36CT, PM_T36CT);
    SAME_CODE(Solar, PM_SOLAR);
    SAME_CODE(FwmTransducer, PM_FWMTRANSDUCER);
    SAME_CODE(Co2Net, PM_CO2_NET);
    SAME_CODE(Co2Rs485, PM_CO2_RS485);
    SAME_CODE(Co2Node, PM_CO2_NODE);
    SAME_CODE(MiniPanel, PM_MINIPANEL);
    SAME_CODE(CsSmAc, PM_CS_SM_AC);
    SAME_CODE(CsSmDc, PM_CS_SM_DC);
    SAME_CODE(CsRsmAc, PM_CS_RSM_AC);
    SAME_CODE(CsRsmDc, PM_CS_RSM_DC);
    SAME_CODE(Pressure, PM_PRESSURE);
    SAME_CODE(Pm5E, PM_PM5E);
    SAME_CODE(HumR, PM_HUM_R);
    SAME_CODE(T322AI, PM_T322AI);
    SAME_CODE(T38AI8AO6DO, PM_T38AI8AO6DO);
    SAME_CODE(PressureSensor, PM_PRESSURE_SENSOR);
    SAME_CODE(T3PT12, PM_T3PT12);
    SAME_CODE(T322AIVG, PM_T322AIVG);
    SAME_CODE(T38IOVG, PM_T38IOVG);
    SAME_CODE(T3PTVG, PM_T3PTVG);
    SAME_CODE(Cm5, PM_CM5);
    SAME_CODE(Pm5EArm, PM_PM5E_ARM);
    SAME_CODE(Stm32Pm25, STM32_PM25);
    SAME_CODE(T332AIArm, PM_T332AI_ARM);
    SAME_CODE(Tstat9, PM_TSTAT9);
    SAME_CODE(MultiSensor, PM_MULTI_SENSOR);
    SAME_CODE(TstatAq, PM_TSTAT_AQ);
    SAME_CODE(ZigbeeRepeater, PM_ZIGBEE_REPEATER);
    SAME_CODE(Tstat6HumChamber, PM_TSTAT6_HUM_Chamber);
    SAME_CODE(AirlabEsp32, PM_AIRLAB_ESP32);
    SAME_CODE(Beeny, PM_BEENY);
    SAME_CODE(WaterSensor, PM_WATER_SENSOR);
    SAME_CODE(T3Lc, PM_T3_LC);
    SAME_CODE(PowerMeter, PM_PWMETER);
    SAME_CODE(MiniPanelArm, PM_MINIPANEL_ARM);
    SAME_CODE(WeatherStation, PM_WEATHER_STATION);
    SAME_CODE(Esp32T38AI8AO6DO, PM_ESP32_T38AI8AO6DO);
    SAME_CODE(Esp32T3Series, PM_ESP32_T3_SERIES);
    SAME_CODE(Esp32T322AI, PM_ESP32_T322AI);
    SAME_CODE(PwmTemperatureTransducer, PWM_TEMPERATURE_TRANSDUCER);
    SAME_CODE(Tstat8Wifi, PM_TSTAT8_WIFI);
    SAME_CODE(Tstat8Occ, PM_TSTAT8_OCC);
    SAME_CODE(Tstat7Arm, PM_TSTAT7_ARM);
    SAME_CODE(Tstat8_220V, PM_TSTAT8_220V);
    SAME_CODE(T36CTA, PM_T36CTA);
    SAME_CODE(Afs, PM_AFS);
    SAME_CODE(FanModule, PM_FAN_MODULE);
    SAME_CODE(Nc, PM_NC);
    SAME_CODE(Tstat8Program, PM_TSTAT8_PROGRAM);
    SAME_CODE(PwmTransducer, PWM_TRANSDUCER);
    SAME_CODE(LightingController, PM_LightingController);
    SAME_CODE(BtuMeter, PM_BTU_METER);
    SAME_CODE(BoatMonitor, PM_BOATMONITOR);
    SAME_CODE(MiniTop, PM_MINI_TOP);
    SAME_CODE(Labbats, PM_LABBATS);
    SAME_CODE(TesterJig, PM_TESTER_JIG);
    SAME_CODE(Stm32Co2Net, STM32_CO2_NET);
    SAME_CODE(Stm32Co2Rs485, STM32_CO2_RS485);
    SAME_CODE(Stm32HumNet, STM32_HUM_NET);
    SAME_CODE(Stm32HumRs485, STM32_HUM_RS485);
    SAME_CODE(Stm32PressureNet, STM32_PRESSURE_NET);
    SAME_CODE(Stm32PressureRs485, STM32_PRESSURE_RS485);
    SAME_CODE(Stm32Co2Node, STM32_CO2_NODE);
    SAME_CODE(ThirdPartyDevice, PM_THIRD_PARTY_DEVICE);

#undef SAME_CODE
}
