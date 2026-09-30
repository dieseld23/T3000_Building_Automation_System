// Tests for the controller with synthetic bootloaders on its bus
// (fake_modbus_controller.h).
//
// Each test plays ISP's side over Modbus TCP as ISP's source has it: an MBAP
// header whose length field is always 00 06, then the device's frame with no
// CRC (ModbusDllforVc\ModbusDllforVc\common.cpp:1838-1848, 4315-4377). The
// first frames are written out as bytes, each with the line of ISP it comes
// from; the rest are built by the helpers here, which are this test's own.
// ISP's transaction id counts up from 1 on every request (common.cpp:51,
// 1838-1840), and so does this test's.

#include "../testing/check.h"
#include "../testing/fake_modbus_controller.h"

#include <stddef.h>

#include <algorithm>
#include <string>

namespace
{
    using namespace t5000::testing;
    using Reply = FakeModbusController::Reply;

    // ISP's first requests of flow A (ComWriter.cpp:2415-2434, 2293-2300).
    const Bytes kControllerRead = { 0x00, 0x01, 0x00, 0x00, 0x00, 0x06, 0xFF, 0x03, 0x00, 0x00, 0x00, 0x64 };
    const Bytes kShutdown       = { 0x00, 0x02, 0x00, 0x00, 0x00, 0x06, 0xFF, 0x06, 0x00, 0x63, 0x00, 0x01 };
    const Bytes kShutdownPoll   = { 0x00, 0x03, 0x00, 0x00, 0x00, 0x06, 0xFF, 0x03, 0x00, 0x63, 0x00, 0x01 };

    struct Isp
    {
        FakeModbusController& controller;
        uint16_t tid = 0;
        int64_t now  = 0;

        // Any request, with ISP's next transaction id and its length field.
        Bytes adu(const Bytes& pdu)
        {
            ++tid;
            Bytes b = { (uint8_t)(tid >> 8), (uint8_t)(tid & 0xFF), 0x00, 0x00, 0x00, 0x06 };
            b.insert(b.end(), pdu.begin(), pdu.end());
            return b;
        }

        Reply send(const Bytes& request)
        {
            const Reply r = controller.request(request, now);
            now += 3100;   // ISP's recv waits 3 s (common.cpp:810-812)
            return r;
        }

        // The reply's bytes, or none when there is none.
        Bytes ask(const Bytes& pdu) { return send(adu(pdu)).bytes; }

        Bytes read(uint8_t unit, uint16_t reg, uint16_t count)
        {
            return ask({ unit, 0x03, (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF), (uint8_t)(count >> 8), (uint8_t)(count & 0xFF) });
        }

        Bytes write(uint8_t unit, uint16_t reg, uint16_t value)
        {
            return ask({ unit, 0x06, (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF), (uint8_t)(value >> 8), (uint8_t)(value & 0xFF) });
        }

        // A block as ISP sends it over TCP: 141 bytes, the length field still
        // 00 06 (common.cpp:4315-4338).
        Bytes block(uint8_t unit, uint16_t address, const Bytes& file, size_t offset)
        {
            Bytes pdu = { unit, 0x10, (uint8_t)(address >> 8), (uint8_t)(address & 0xFF), 0x00, 0x80, 0x80 };
            for (size_t i = 0; i < (size_t)kBlockBytes; i++)
                pdu.push_back(offset + i < file.size() ? file[offset + i] : 0xFF);
            return adu(pdu);
        }
    };

    // The reply as ISP reads it: the MBAP header skipped (common.cpp:9365).
    uint16_t reg_in(const Bytes& reply, int index) { return (uint16_t)(reply[9 + 2 * index] << 8 | reply[10 + 2 * index]); }

    // A write's echo, as ISP checks it: bytes 6-10 the request's (common.cpp:3002).
    bool echoes(const Bytes& reply, uint8_t unit, uint16_t reg, uint16_t value)
    {
        return reply.size() == 12 && reply[6] == unit && reply[7] == 0x06 && reply[8] == (reg >> 8) &&
               reply[9] == (reg & 0xFF) && reply[10] == (value >> 8) && reply[11] == (value & 0xFF);
    }

    // A block's reply, as ISP checks it: bytes 6-11 the request's (common.cpp:4372-4374).
    bool block_answered(const Reply& r, const Bytes& request)
    {
        return !r.silent && r.bytes.size() == 12 && std::equal(r.bytes.begin() + 6, r.bytes.end(), request.begin() + 6);
    }

    Bytes file_of(size_t size, uint8_t seed)
    {
        Bytes b(size);
        for (size_t i = 0; i < size; i++)
            b[i] = (uint8_t)(i * 5 + seed);
        return b;
    }

    FakeBootloader device(uint8_t unit, uint16_t product)
    {
        FakeBootloader d;
        d.set_identity(unit, 7000 + unit, product, 538, 62);
        return d;
    }

    FakeModbusController controller_with(std::vector<FakeBootloader> devices)
    {
        FakeModbusController c;
        c.devices = std::move(devices);
        return c;
    }

    void test_isp_flashes_an_arm_hex_through_a_controller(bool handshake)
    {
        section(handshake ? "synthetic controller: flow A, an ARM .hex through a controller at 63.6"
                          : "synthetic controller: flow A through a controller at 63.5, with no handshake");
        FakeBootloader d = device(5, 74);
        d.echoes_init_and_erase = false;
        FakeModbusController c = controller_with({ d });
        c.shutdown_polls_busy = 1;
        if (!handshake)
            c.registers[4] = 5;
        Isp isp{ c };
        const Bytes one = file_of(2 * 128, 3);

        // The controller's own registers, unit 255, 100 of them (:2415-2419).
        Reply r = isp.send(isp.adu({ 0xFF, 0x03, 0x00, 0x00, 0x00, 0x64 }));
        check(isp.tid == 1 && c.heard[0].request == kControllerRead, "ISP's first request, byte for byte");
        if (require(r.bytes.size() == 209, "its reply is 209 bytes"))
        {
            check(r.bytes[0] == 0x00 && r.bytes[1] == 0x01, "with the request's transaction id");
            check(r.bytes[4] == 0x00 && r.bytes[5] == 0xCB, "and a length of 203");
            check(r.bytes[6] == 0xFF && r.bytes[7] == 0x03 && r.bytes[8] == 0xC8, "unit 255, fc 3, 200 bytes, as ISP checks");
            check(reg_in(r.bytes, 7) == 74 && (reg_in(r.bytes, 5) * 10 + reg_in(r.bytes, 4) >= 636) == handshake,
                  handshake ? "a MiniPanel ARM at 63.6 or later, which ISP asks to quiet its bus (:2423-2431)"
                            : "a MiniPanel ARM before 63.6, which ISP does not ask (:2428-2429)");
        }

        // Register 99 (:2278-2332).
        if (handshake)
        {
            check(isp.ask({ 0xFF, 0x06, 0x00, 0x63, 0x00, 0x01 }) == kShutdown, "99 = 1 is echoed");
            check(c.heard[1].request == kShutdown, "as ISP sent it");
            Bytes p = isp.ask({ 0xFF, 0x03, 0x00, 0x63, 0x00, 0x01 });
            check(c.heard[2].request == kShutdownPoll, "ISP's poll, byte for byte");
            check(p.size() == 11 && reg_in(p, 0) == kShutdownStart, "the first poll says 1, still working");
            p = isp.read(0xFF, 99, 1);
            check(p.size() == 11 && reg_in(p, 0) == kShutdownSuccess, "the second, 2: done");
        }

        // The device, unit 5 (:1983-2136, 2526-2564).
        const Bytes id = isp.read(5, 0, 18);
        check(id.size() == 45 && id[6] == 5 && id[8] == 36 && reg_in(id, 7) == 74, "the device's 18 registers, relayed");
        check(echoes(isp.write(5, 16, 127), 5, 16, 127), "the jump is echoed");
        const Bytes r11 = isp.read(5, 11, 1);
        check(r11.size() == 11 && reg_in(r11, 0) == 62, "register 11 past 0");
        const Bytes st = isp.read(5, 0xEE10, 1);
        check(st.size() == 11 && reg_in(st, 0) == 0, "0xEE10 not interrupted (:2611-2636)");
        for (int i = 0; i < 3; i++)
            check(isp.write(5, 16, 0x7F).empty(), "0x7F not echoed by an ARM bootloader, three tries (:2749-2776)");
        for (int i = 0; i < 3; i++)
            check(isp.write(5, 16, 0x3F).empty(), "nor 0x3F (:2783-2807)");
        check(isp.read(5, 65010, 1).size() == 11, "register 65010 (:2827)");
        check(echoes(isp.write(5, 16, 0x1F), 5, 16, 0x1F), "0x1F is echoed");
        check(echoes(isp.write(5, 12, 1), 5, 12, 1), "register 12 = 1, section 1 (:1074)");
        int good = 0;
        for (size_t at = 0; at < one.size(); at += kBlockBytes)
        {
            const Bytes b = isp.block(5, (uint16_t)at, one, at);
            check_eq((long)b.size(), 141, "a block is 141 bytes over TCP");
            good += block_answered(isp.send(b), b) ? 1 : 0;
        }
        check_eq(good, 2, "each block answered with bytes 6-11 of its request");
        check(echoes(isp.write(5, 16, 1), 5, 16, 1), "the end");

        check_eq(c.frames_not_isp(), 0, "nothing sent was outside ISP's flash");
        check(c.device(5)->image().size() == 0x10000 + one.size() &&
                  std::equal(one.begin(), one.end(), c.device(5)->image().begin() + 0x10000),
              "section 1 at 0x10000");
        check_eq((long)c.transaction_ids.size(), (long)isp.tid, "every transaction id kept");
        check(c.transaction_ids.front() == 1 && c.transaction_ids.back() == isp.tid, "from 1 up");
    }

    void test_isp_flashes_a_data_hex_through_a_controller()
    {
        section("synthetic controller: flow B, a .hex of data records through a controller");
        FakeModbusController c = controller_with({ device(5, 9) });
        Isp isp{ c };
        const Bytes file = file_of(3 * 128, 8);

        const Bytes id = isp.read(5, 0, 40);   // :404-457
        check(id.size() == 89 && id[8] == 80 && reg_in(id, 6) == 5 && reg_in(id, 7) == 9,
              "40 registers relayed: register 6 the id, 7 a TStat8");
        check(echoes(isp.write(5, 16, 127), 5, 16, 127), "the jump (:459-482)");
        check(echoes(isp.write(5, 16, 1), 5, 16, 1), "the reset, register 11 under 37 (:582-606)");
        check(isp.read(5, 0xEE10, 1).size() == 11, "0xEE10 (:857)");
        for (uint16_t reg = 1993; reg <= 1998; reg++)
            check(echoes(isp.write(5, reg, (uint16_t)(reg + 1)), 5, reg, (uint16_t)(reg + 1)), "MD5 and size (:931-937)");
        check(echoes(isp.write(5, 16, 0x7F), 5, 16, 0x7F), "0x7F");
        check(echoes(isp.write(5, 16, 0x3F), 5, 16, 0x3F), "0x3F");
        check(echoes(isp.write(5, 16, 0x1F), 5, 16, 0x1F), "0x1F");
        int good = 0;
        for (size_t at = 0; at < file.size(); at += kBlockBytes)
        {
            const Bytes b = isp.block(5, (uint16_t)at, file, at);
            good += block_answered(isp.send(b), b) ? 1 : 0;
        }
        check_eq(good, 3, "three blocks");
        const Bytes wake = isp.read(0xFF, 1, 1);
        check(wake.size() == 11 && wake[6] == 0xFF, "the wake-up read to 255 is the controller's to answer (:1013)");
        check(echoes(isp.write(5, 16, 1), 5, 16, 1), "the end (:1026-1038)");

        check_eq(c.frames_not_isp(), 0, "nothing sent was outside ISP's flash");
        check(c.device(5)->image() == file, "the device holds the file");
        check_eq(c.device(5)->update_registers.at(1996), 1997, "and the MD5 words");
    }

    void test_requests_isp_would_not_send()
    {
        section("synthetic controller: requests ISP would not send");
        FakeModbusController c = controller_with({ device(5, 9) });
        Isp isp{ c };

        Bytes standard = isp.adu({ 5, 0x03, 0x00, 0x00, 0x00, 0x12 });
        standard[5] = 0x06;
        standard[4] = 0x00;
        standard[2] = 0x00;
        standard[3] = 0x01;
        check(!isp.send(standard).silent, "a protocol id of 1 is still answered");
        check_eq(c.frames_not_isp(), 1, "but judged");

        Bytes length = isp.adu({ 5, 0x03, 0x00, 0x00, 0x00, 0x12 });
        length[5] = 0x07;
        isp.send(length);
        check_eq(c.frames_not_isp(), 2, "a length field of 7 is judged: ISP sends 6");

        const Bytes fc4 = isp.adu({ 5, 0x04, 0x00, 0x00, 0x00, 0x01 });
        check(isp.send(fc4).silent, "function 4 is not a request ISP frames, and is not answered");
        check_eq(c.frames_not_isp(), 3, "and judged");

        Bytes shortened = isp.adu({ 5, 0x03, 0x00, 0x00, 0x00, 0x12 });
        shortened.pop_back();
        check(isp.send(shortened).silent, "a read a byte short is not answered");
        check_eq(c.frames_not_isp(), 4, "and judged");

        check(!isp.read(0xFF, 0, 10).empty(), "unit 255 answers a read of 10");
        check_eq(c.frames_not_isp(), 5, "judged: ISP reads 100");
        check(!isp.write(0xFF, 5, 1).empty(), "and a write of register 5");
        check_eq(c.frames_not_isp(), 6, "judged: ISP writes only 99 = 1");

        check(isp.read(7, 0, 18).empty(), "a unit with no device on the bus gets nothing");
        check_eq(c.frames_not_isp(), 6, "and is not judged: T5000 may ask for a device that is not there");

        Bytes block = isp.block(5, 0, Bytes(128, 0x11), 0);
        block[5] = 0x87;
        isp.send(block);
        check_eq(c.frames_not_isp(), 8, "a block with the length field 0x87 is judged, and the device judges it as not after 0x1F");
        const std::vector<std::string> reasons = c.not_isp_reasons();
        check(reasons.size() == 8 && reasons.back().find("unit 5") == 0, "the device's reasons named by its unit");
    }

    void test_the_shutdown_register()
    {
        section("synthetic controller: register 99");
        {
            FakeModbusController c;
            c.shutdown_result = kShutdownTimeout;
            Isp isp{ c };
            isp.write(0xFF, 99, 1);
            const Bytes p = isp.read(0xFF, 99, 1);
            check(p.size() == 11 && reg_in(p, 0) == 3, "3: the bus could not be quieted (:2303-2309)");
        }
        {
            FakeModbusController c;
            c.shutdown_polls_busy = 100;
            Isp isp{ c };
            isp.write(0xFF, 99, 1);
            int working = 0;
            for (int i = 0; i < 10; i++)
                working += reg_in(isp.read(0xFF, 99, 1), 0) == kShutdownStart ? 1 : 0;
            check_eq(working, 10, "1 on all ten of ISP's polls (:2293-2300)");
        }
        {
            FakeModbusController c;
            c.echoes_shutdown = false;
            Isp isp{ c };
            check(isp.write(0xFF, 99, 1).empty(), "99 = 1 unanswered");
            check_eq(c.shutdown_state(), kShutdownInitial, "and not acted on");
            check_eq(reg_in(isp.read(0xFF, 99, 1), 0), 0, "99 reads 0");
        }

        // Whom ISP asks: a MiniPanel, TStat10, MiniPanel ARM or ESP32 T3, at
        // 63.6 or later (ComWriter.cpp:2423-2431; ProductModel.h).
        const auto judged = [](uint16_t model, uint16_t major, uint16_t minor) {
            FakeModbusController c;
            c.registers[7] = model;
            c.registers[5] = major;
            c.registers[4] = minor;
            Isp isp{ c };
            isp.write(0xFF, 99, 1);
            return c.frames_not_isp();
        };
        check_eq(judged(35, 63, 6) + judged(10, 63, 6) + judged(74, 63, 6) + judged(88, 63, 6), 0,
                 "99 = 1 to each of the four models at 63.6 is ISP's");
        check_eq(judged(74, 64, 0), 0, "and at 64.0");
        check_eq(judged(74, 63, 5), 1, "at 63.5 it is judged");
        check_eq(judged(9, 63, 6), 1, "and to a TStat8, which ISP never asks");
        check_eq(judged(36, 70, 0), 1, "or to model 36");
    }

    void test_faults_on_the_connection()
    {
        section("synthetic controller: faults on the connection");
        using Action = FakeModbusController::Fault::Action;
        FakeModbusController c = controller_with({ device(5, 9) });
        FakeModbusController::Fault silence;
        silence.unit = 5;
        silence.fc = 3;
        silence.action = Action::Silence;
        FakeModbusController::Fault late;
        late.fc = 6;
        late.action = Action::Late;
        late.delay_ms = 3500;
        FakeModbusController::Fault split;
        split.reg = 11;
        split.action = Action::Split;
        FakeModbusController::Fault reset;
        reset.reg = 0xEE10;
        reset.action = Action::Reset;
        FakeModbusController::Fault close;
        close.reg = 65010;
        close.action = Action::Close;
        c.faults = { silence, late, split, reset, close };
        c.relay_ms = 40;
        Isp isp{ c };

        check(isp.send(isp.adu({ 5, 0x03, 0x00, 0x00, 0x00, 0x28 })).silent, "a read gets no reply");
        const Reply w = isp.send(isp.adu({ 5, 0x06, 0x00, 0x10, 0x00, 0x7F }));
        check(!w.silent && w.delay_ms == 3540, "a reply 3.5 s late, past ISP's 3 s, on top of the bus's 40 ms");
        const Reply s = isp.send(isp.adu({ 5, 0x03, 0x00, 0x0B, 0x00, 0x01 }));
        check(!s.silent && s.split_at == 5 && s.bytes.size() == 11, "a reply in two pieces, which ISP's one recv cuts short");
        check_eq(s.delay_ms, 40, "the bus's time on every relayed reply");

        const Reply r = isp.send(isp.adu({ 5, 0x03, 0xEE, 0x10, 0x00, 0x01 }));
        check(r.silent && r.after == Reply::After::Reset && !c.connected, "a reset, and no reply");
        check(isp.send(isp.adu({ 5, 0x03, 0x00, 0x0B, 0x00, 0x01 })).silent, "nothing is heard until the client connects again");
        c.connect();
        check_eq(c.connections, 2, "a second connection");
        check(c.device(5)->mode == FakeBootloader::Mode::Bootloader, "and the device is still its bootloader");
        check(!isp.send(isp.adu({ 5, 0x03, 0x00, 0x0B, 0x00, 0x01 })).silent, "answered on the new connection");

        const Reply cl = isp.send(isp.adu({ 5, 0x03, 0xFD, 0xF2, 0x00, 0x01 }));
        check(!cl.silent && cl.after == Reply::After::Close && !c.connected, "a reply, then the connection closed");

        c.connect();
        FakeBootloader& d = *c.device(5);
        ModbusFault garble;
        garble.action = ModbusFault::Action::Garble;
        d.faults.push_back(garble);
        check(isp.send(isp.adu({ 5, 0x06, 0x00, 0x10, 0x00, 0x1F })).silent,
              "a device's reply garbled on the bus is dropped by the controller");
    }

    void test_several_units_and_how_requests_are_cut()
    {
        section("synthetic controller: several units, and cutting requests out of a stream");
        FakeModbusController c = controller_with({ device(5, 9), device(6, 74) });
        Isp isp{ c };
        const Bytes r = isp.read(6, 0, 18);
        check(r.size() == 45 && r[6] == 6 && reg_in(r, 7) == 74, "unit 6 answers for itself");
        check(c.device(5)->heard.empty(), "and unit 5 heard nothing");

        check_eq(FakeModbusController::request_length(Bytes(7, 0)), 0, "7 bytes are not enough to tell");
        const Bytes read = isp.adu({ 5, 0x03, 0x00, 0x00, 0x00, 0x12 });
        check_eq(FakeModbusController::request_length(read), 12, "a read is 12 bytes");
        const Bytes block = isp.block(5, 0, Bytes(128, 1), 0);
        check_eq(FakeModbusController::request_length(Bytes(block.begin(), block.begin() + 12)), 0,
                 "a block's first 12 bytes are not enough");
        check_eq(FakeModbusController::request_length(block), 141, "a block is 13 and its byte count, whatever its length field says");
        check_eq(FakeModbusController::request_length(isp.adu({ 5, 0x04, 0, 0, 0, 1 })), -1, "function 4 is none of ISP's");
    }
}

int run_fake_modbus_controller_tests()
{
    test_isp_flashes_an_arm_hex_through_a_controller(true);
    test_isp_flashes_an_arm_hex_through_a_controller(false);
    test_isp_flashes_a_data_hex_through_a_controller();
    test_requests_isp_would_not_send();
    test_the_shutdown_register();
    test_faults_on_the_connection();
    test_several_units_and_how_requests_are_cut();
    return 0;
}
