// Tests for the synthetic bootloader on a serial line (fake_bootloader.h,
// fake_rtu_bus.h).
//
// Each test plays ISP's side of a flash as ISP's source has it, frame by
// frame. The frames are literal bytes, their CRCs worked out apart from this
// code, each with the line of ISP it comes from; only the 137-byte blocks
// are built here, and one of them is checked against a CRC worked out apart
// too. ISP waits 520 ms for most replies (20 ms a byte and 360, common.cpp:
// 5081-5087), and a test's clock moves on by that when nothing comes.
//
// What is tested is that the fake answers as ISP needs, judges what ISP
// would not send, and does what its faults say. Whether a real bootloader
// does the same is the owner's to see on hardware.

#include "../testing/check.h"
#include "../testing/fake_rtu_bus.h"

#include <stddef.h>

#include <algorithm>
#include <string>

namespace
{
    using namespace t5000::testing;

    // ISP's frames to id 1, with their CRCs.
    const Bytes kRead40   = { 0x01, 0x03, 0x00, 0x00, 0x00, 0x28, 0x45, 0xD4 };   // ComWriter.cpp:405-448
    const Bytes kRead18   = { 0x01, 0x03, 0x00, 0x00, 0x00, 0x12, 0xC5, 0xC7 };   // :1983-2209
    const Bytes kJump     = { 0x01, 0x06, 0x00, 0x10, 0x00, 0x7F, 0xC9, 0xEF };   // :459-482, 1479, 2529
    const Bytes kEnd      = { 0x01, 0x06, 0x00, 0x10, 0x00, 0x01, 0x49, 0xCF };   // :1026-1038, 2976-2993
    const Bytes kErase    = { 0x01, 0x06, 0x00, 0x10, 0x00, 0x3F, 0xC8, 0x1F };   // :951
    const Bytes kStart    = { 0x01, 0x06, 0x00, 0x10, 0x00, 0x1F, 0xC9, 0xC7 };   // :964
    const Bytes kAfter0   = { 0x01, 0x06, 0x00, 0x10, 0x00, 0x08, 0x89, 0xC9 };   // :1742
    const Bytes kBank0    = { 0x01, 0x06, 0x00, 0x21, 0x00, 0x00, 0xD9, 0xC0 };   // :1604
    const Bytes kBank1    = { 0x01, 0x06, 0x00, 0x21, 0x00, 0x01, 0x18, 0x00 };   // :1752
    const Bytes kStatus   = { 0x01, 0x03, 0xEE, 0x10, 0x00, 0x01, 0xB0, 0xE7 };   // :857, 2614
    const Bytes kReg11    = { 0x01, 0x03, 0x00, 0x0B, 0x00, 0x01, 0xF5, 0xC8 };   // :1487-1503, 2540
    const Bytes kMd5      = { 0x01, 0x03, 0x07, 0xC9, 0x00, 0x04, 0x95, 0x43 };   // :868
    const Bytes kPackets  = { 0x01, 0x03, 0x07, 0xC7, 0x00, 0x01, 0x34, 0x83 };   // :833
    const Bytes kCpu      = { 0x01, 0x03, 0xFD, 0xF2, 0x00, 0x01, 0x14, 0x55 };   // :2827
    const Bytes kQuiet    = { 0xFF, 0x06, 0x00, 0x10, 0x04, 0x55, 0x5F, 0x2E };   // :2515, 2903
    const Bytes kWake     = { 0xFF, 0x03, 0x00, 0x01, 0x00, 0x01, 0xC0, 0x14 };   // :1013
    const Bytes kSection1 = { 0x01, 0x06, 0x00, 0x0C, 0x00, 0x01, 0x88, 0x09 };   // :1074-1093
    const Bytes kSection2 = { 0x01, 0x06, 0x00, 0x0C, 0x00, 0x02, 0xC8, 0x08 };
    const Bytes kEsp23    = { 0x01, 0x03, 0x00, 0x17, 0x00, 0x01, 0x34, 0x0E };   // :1770
    const Bytes kRead1994 = { 0x01, 0x03, 0x07, 0xCA, 0x00, 0x06, 0xE4, 0x82 };   // :1442
    const Bytes kW1993    = { 0x01, 0x06, 0x07, 0xC9, 0x12, 0x34, 0x55, 0xF7 };   // :932
    const Bytes kW1995    = { 0x01, 0x06, 0x07, 0xCB, 0xAB, 0xCD, 0x47, 0xE5 };   // :1474

    // Replies ISP accepts, with their CRCs.
    const Bytes kReadsIdle         = { 0x01, 0x03, 0x02, 0x00, 0x00, 0xB8, 0x44 };
    const Bytes kReadsInterrupted  = { 0x01, 0x03, 0x02, 0x00, 0x1F, 0xF9, 0x8C };
    const Bytes kReg11Is62         = { 0x01, 0x03, 0x02, 0x00, 0x3E, 0x39, 0x94 };
    const Bytes kPacketsAre2       = { 0x01, 0x03, 0x02, 0x00, 0x02, 0x39, 0x85 };
    const Bytes kBlockReply0       = { 0x01, 0x10, 0x00, 0x00, 0x00, 0x80, 0xC1, 0xA9 };
    const Bytes kBlockReply80      = { 0x01, 0x10, 0x00, 0x80, 0x00, 0x80, 0xC0, 0x41 };

    // A block as ISP frames it (common.cpp:4225-4241): 00 80 is the byte
    // count where Modbus has a register count, and 80 the byte count again.
    Bytes block_frame(uint8_t id, uint16_t address, const Bytes& file, size_t offset)
    {
        Bytes f = { id, 0x10, (uint8_t)(address >> 8), (uint8_t)(address & 0xFF), 0x00, 0x80, 0x80 };
        for (size_t i = 0; i < (size_t)kBlockBytes; i++)
            f.push_back(offset + i < file.size() ? file[offset + i] : 0xFF);
        add_line_crc(f);
        return f;
    }

    Bytes write_frame(uint8_t id, uint16_t reg, uint16_t value)
    {
        Bytes f = { id, 0x06, (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF), (uint8_t)(value >> 8), (uint8_t)(value & 0xFF) };
        add_line_crc(f);
        return f;
    }

    Bytes file_of(size_t size, uint8_t seed)
    {
        Bytes b(size);
        for (size_t i = 0; i < size; i++)
            b[i] = (uint8_t)(i * 7 + seed);
        return b;
    }

    uint16_t reg_in(const Bytes& reply, int index) { return (uint16_t)(reply[3 + 2 * index] << 8 | reply[4 + 2 * index]); }

    // ISP's side of the line, with a clock of its own.
    struct Isp
    {
        FakeRtuBus& bus;
        int64_t now = 0;

        // Sends a frame after purging the line, as ISP does, and takes what
        // arrives within the window.
        Bytes ask(const Bytes& frame, int window_ms = 520)
        {
            bus.purge(now);
            bus.send(frame, now);
            now += window_ms;
            return bus.receive(now);
        }

        void wait(int ms) { now += ms; }
    };

    // Blocks from `offset` of the file at addresses from `first`, each
    // checked for ISP's reply: its first six bytes, and a good CRC
    // (common.cpp:4305-4312).
    int send_blocks(Isp& isp, uint8_t id, const Bytes& file, size_t offset, size_t length, uint16_t first = 0)
    {
        int good = 0;
        for (size_t at = 0; at < length; at += kBlockBytes)
        {
            const uint16_t address = (uint16_t)(first + at);
            const Bytes f = block_frame(id, address, file, offset + at);
            const Bytes r = isp.ask(f);
            if (r.size() == 8 && std::equal(r.begin(), r.begin() + 6, f.begin()) && line_crc_ok(r))
                good++;
        }
        return good;
    }

    bool same(const Bytes& image, size_t at, const Bytes& file, size_t offset, size_t length)
    {
        if (image.size() < at + length || file.size() < offset + length)
            return false;
        return std::equal(file.begin() + (ptrdiff_t)offset, file.begin() + (ptrdiff_t)(offset + length),
                          image.begin() + (ptrdiff_t)at);
    }

    FakeRtuBus bus_with(FakeBootloader d)
    {
        FakeRtuBus bus;
        bus.devices.push_back(d);
        return bus;
    }

    FakeBootloader device(uint16_t product)
    {
        FakeBootloader d;
        d.set_identity(1, 12345678, product, 538, 62);
        return d;
    }

    // --- the frames and the CRC ------------------------------------------

    void test_the_frames_are_modbus()
    {
        section("synthetic bootloader: the CRC and a block");
        const uint8_t textbook[] = { 0x01, 0x03, 0x00, 0x00, 0x00, 0x01 };
        check_eq(line_crc(textbook, sizeof(textbook)), 0x0A84, "01 03 00 00 00 01 has the CRC 84 0A");

        Bytes data(128);
        for (int i = 0; i < 128; i++)
            data[i] = (uint8_t)i;
        const Bytes f = block_frame(1, 0x0080, data, 0);
        check_eq((long)f.size(), 137, "a block is 137 bytes on the line");
        check(f[135] == 0x10 && f[136] == 0xB2, "and a block of 0..127 at 0x0080 has the CRC 10 B2");
    }

    // --- ISP's three serial threads, clean -------------------------------

    void test_isp_flashes_a_data_hex()
    {
        section("synthetic bootloader: Flash_Modebus_Device, a .hex of data records");
        FakeRtuBus bus = bus_with(device(9));   // a TStat8
        Isp isp{ bus };
        const Bytes file = file_of(3 * 128, 11);

        // The identity, 40 registers, before the jump (ComWriter.cpp:405-448).
        Bytes r = isp.ask(kRead40);
        if (require(r.size() == 85, "40 registers come back in 85 bytes"))
        {
            check(r[0] == 0x01 && r[1] == 0x03 && r[2] == 0x50, "id 1, fc 3, 80 bytes, as ISP checks them");
            check(line_crc_ok(r), "with a good CRC");
            check_eq(reg_in(r, 6), 1, "register 6, the id ISP uses from now on (:493)");
            check_eq(reg_in(r, 7), 9, "register 7, the product");
            check_eq(reg_in(r, 11), 1, "register 11 in the application, under 37 (:506, 588)");
        }

        check(isp.ask(kJump) == kJump, "127 to register 16 is echoed exactly (common.cpp:2823-2830)");
        check(bus.devices[0].mode == FakeBootloader::Mode::Bootloader, "and the device is its bootloader");
        check(isp.ask(kEnd) == kEnd, "ISP's reset before the blocks is echoed (:582-606)");
        check(bus.devices[0].mode == FakeBootloader::Mode::Bootloader, "and the device stays in its bootloader");

        // flash_a_tstat (:839-1039).
        check(isp.ask(kStatus) == kReadsIdle, "0xEE10 reads 0: not interrupted (:857-861)");
        check(isp.ask(kW1993) == kW1993, "the MD5's first word is echoed (:932)");
        for (uint16_t reg = 1994; reg <= 1998; reg++)
        {
            const Bytes w = write_frame(1, reg, (uint16_t)(reg * 3));
            check(isp.ask(w) == w, "the rest of the MD5 and the size are echoed (:932-937)");
        }
        check(isp.ask(kJump) == kJump, "0x7F, init, is echoed (:940)");
        check(isp.ask(kErase) == kErase, "0x3F, erase, is echoed (:951)");
        isp.wait(7000);
        check(isp.ask(kStart) == kStart, "0x1F, start, is echoed (:964)");
        check_eq(send_blocks(isp, 1, file, 0, file.size()), 3, "each block is answered with its first six bytes");
        check(isp.ask(kEnd) == kEnd, "16=1, the end, is echoed (:1030)");

        const FakeBootloader& d = bus.devices[0];
        check_eq(d.frames_not_isp(), 0, "nothing sent was outside ISP's flash");
        check(d.image() == file, "the flash holds the file");
        check_eq(d.update_registers.at(1993), 0x1234, "the MD5's first word is kept");
        check_eq(d.update_registers.at(1998), 1998 * 3, "and the size");
        check_eq(d.erases, 1, "one erase");
        check_eq(d.status, 0, "0xEE10 is 0 again after the end");
        check_eq(d.packets(), 3, "register 1991 counts the three blocks");
        check_eq((long)bus.sent.size(), 17, "every frame sent is kept");
    }

    void test_isp_flashes_a_bin()
    {
        section("synthetic bootloader: flashThread_ForExtendFormatHexfile, a .bin");
        FakeRtuBus bus = bus_with(device(74));
        Isp isp{ bus };
        const Bytes file = file_of(2 * 128 + 40, 5);

        const Bytes r = isp.ask(kRead18);
        check(r.size() == 41 && r[2] == 0x24 && line_crc_ok(r), "18 registers come back in 41 bytes (:1983-2209)");
        check(isp.ask(kJump) == kJump, "the jump is echoed (:1479)");
        check(isp.ask(kReg11) == kReg11Is62, "register 11 reads 62 in the bootloader: past 1, so ready (:1487-1503)");
        check(isp.ask(kReg11) == kReg11Is62, "and 37 or more, the 128K chip (:1577-1612)");
        check(isp.ask(kBank0) == kBank0, "33=0 is echoed");

        check(isp.ask(kStatus) == kReadsIdle, "not interrupted");
        check(isp.ask(kJump) == kJump, "init");
        check(isp.ask(kErase) == kErase, "erase");
        isp.wait(7000);
        check(isp.ask(kStart) == kStart, "start");
        check_eq(send_blocks(isp, 1, file, 0, file.size()), 3, "three blocks, the last padded");
        check(isp.ask(kEnd) == kEnd, "the end");
        check(isp.ask(kAfter0) == kAfter0, "16=8 after section 0 is echoed, after the end (:1742)");
        check(isp.ask(kBank1) == kBank1, "and 33=1, which ISP must have (:1749-1763)");

        const FakeBootloader& d = bus.devices[0];
        check_eq(d.frames_not_isp(), 0, "nothing sent was outside ISP's flash");
        const Bytes image = d.image();
        check(same(image, 0, file, 0, file.size()), "the flash holds the file");
        check_eq((long)image.size(), 3 * 128, "in three whole blocks");
    }

    void test_a_bin_past_64k_wraps()
    {
        section("synthetic bootloader: a .bin past 64 KB, its addresses coming round");
        FakeBootloader d = device(74);
        d.mode = FakeBootloader::Mode::Bootloader;
        FakeRtuBus bus = bus_with(d);
        Isp isp{ bus };
        const Bytes file = file_of(0x10000 + 2 * 128, 3);

        isp.ask(kStart);
        // ISP sends ii & 0xFFFF and has no bank register in this thread
        // (GF:1282, common.cpp:4218).
        int good = 0;
        for (size_t at = 0; at < file.size(); at += kBlockBytes)
        {
            const Bytes f = block_frame(1, (uint16_t)(at & 0xFFFF), file, at);
            const Bytes r = isp.ask(f);
            good += r.size() == 8 && std::equal(r.begin(), r.begin() + 6, f.begin()) ? 1 : 0;
        }
        check_eq(good, 514, "every block is answered");
        check_eq(bus.devices[0].frames_not_isp(), 0, "and 0xFF80 then 0 is in sequence");
        const Bytes image = bus.devices[0].image();
        check(image == file, "the flash holds the file, the blocks past 64 KB after the first 64 KB");
    }

    void test_isp_flashes_two_sections()
    {
        section("synthetic bootloader: an ASIX .hex of two sections, one after the other");
        FakeBootloader d = device(6);
        d.mode = FakeBootloader::Mode::Bootloader;
        FakeRtuBus bus = bus_with(d);
        Isp isp{ bus };
        const Bytes low  = file_of(2 * 128, 1);
        const Bytes high = file_of(2 * 128, 2);

        // Each section runs all of flash_a_tstat (:1624-1768).
        for (int p = 0; p < 2; p++)
        {
            check(isp.ask(kStatus) == kReadsIdle, "not interrupted");
            isp.ask(kJump);
            isp.ask(kErase);
            isp.wait(7000);
            isp.ask(kStart);
            check_eq(send_blocks(isp, 1, p == 0 ? low : high, 0, 256), 2, "two blocks, from address 0");
            check(isp.ask(kEnd) == kEnd, "the end");
            if (p == 0)
            {
                check(isp.ask(kAfter0) == kAfter0, "16=8");
                check(isp.ask(kBank1) == kBank1, "33=1, the second bank");
            }
        }

        const FakeBootloader& dev = bus.devices[0];
        check_eq(dev.frames_not_isp(), 0, "nothing sent was outside ISP's flash");
        check_eq(dev.erases, 2, "each section erased");
        const Bytes image = dev.image();
        check(same(image, 0, low, 0, 256), "section 0 in bank 0");
        check(same(image, 0x10000, high, 0, 256), "section 1 in bank 1, not over section 0");
    }

    void test_isp_flashes_an_arm_hex()
    {
        section("synthetic bootloader: flashThread_ForExtendFormatHexfile_RAM, an ARM .hex");
        FakeBootloader d = device(74);
        d.echoes_init_and_erase = false;   // as an ARM bootloader is said to (:2754)
        FakeRtuBus bus = bus_with(d);
        Isp isp{ bus };
        const Bytes one = file_of(2 * 128, 7);
        const Bytes two = file_of(128, 9);

        check(isp.ask(kQuiet).empty(), "16 = 0x0455 to 255 is not answered (:2514-2516)");
        check(isp.ask(kRead18).size() == 41, "the identity");
        check(isp.ask(kJump) == kJump, "the jump");
        isp.wait(2000);
        check(isp.ask(kReg11) == kReg11Is62, "register 11 past 0 (:2539-2582)");
        check(isp.ask(kStatus) == kReadsIdle, "0xEE10, not 0x40 (:2614)");
        check(isp.ask(kStatus) == kReadsIdle, "0xEE10, not 0x1F (:2636)");
        for (int i = 0; i < 3; i++)
            check(isp.ask(kJump).empty(), "0x7F is not echoed, three tries (:2749-2776)");
        for (int i = 0; i < 3; i++)
            check(isp.ask(kErase).empty(), "nor 0x3F (:2783-2807)");
        isp.wait(7000);
        const Bytes cpu = isp.ask(kCpu);
        check(cpu.size() == 7 && line_crc_ok(cpu), "register 65010 is read (:2827)");
        check(isp.ask(kStart) == kStart, "0x1F is echoed (:2839-2865)");
        check(isp.ask(kQuiet).empty(), "and 0x0455 to 255 again (:2903)");

        // An ARM 64K .hex: section 0, the bootloader's, is not sent
        // (:1043-1175); each other one after its register 12.
        check(isp.ask(kSection1) == kSection1, "register 12 = 1 is echoed");
        check_eq(send_blocks(isp, 1, one, 0, one.size()), 2, "section 1's blocks from 0");
        check(isp.ask(kSection2) == kSection2, "register 12 = 2");
        check_eq(send_blocks(isp, 1, two, 0, two.size()), 1, "section 2's block from 0");
        check(isp.ask(kEnd) == kEnd, "the end (:2976-2993)");

        const FakeBootloader& dev = bus.devices[0];
        check_eq(dev.frames_not_isp(), 0, "nothing sent was outside ISP's flash, the broadcasts included");
        check_eq(dev.writes_of(kRegCommand, kCmdJump), 4, "four 0x7F heard: the jump and three inits");
        check_eq(dev.writes_of(kRegCommand, kCmdQuiet), 2, "both broadcasts heard");
        const Bytes image = dev.image();
        check(same(image, 0x10000, one, 0, one.size()), "section 1 at 0x10000");
        check(same(image, 0x20000, two, 0, two.size()), "section 2 at 0x20000");
    }

    // --- resume ----------------------------------------------------------

    void test_a_cut_arm_flash_resumes()
    {
        section("synthetic bootloader: an ARM flash cut short, and resumed");
        FakeBootloader d = device(74);
        d.cut_off_after_blocks = 2;
        FakeRtuBus bus = bus_with(d);
        Isp isp{ bus };
        const Bytes file = file_of(4 * 128, 4);

        isp.ask(kJump);
        isp.ask(kJump);
        isp.ask(kErase);
        isp.ask(kStart);
        isp.ask(kSection1);
        check_eq(send_blocks(isp, 1, file, 0, file.size()), 2, "two blocks are answered, then nothing");
        FakeBootloader& dev = bus.devices[0];
        check(dev.cut_off(), "the device is cut off");
        check(isp.ask(kStatus).empty(), "and answers nothing");
        check_eq((long)dev.blocks.size(), 2, "the third block was not taken");

        dev.power_on(isp.now);
        dev.cut_off_after_blocks = -1;
        check(dev.mode == FakeBootloader::Mode::Bootloader, "it comes back in its bootloader");
        const Bytes id = isp.ask(kRead18);
        check(id.size() == 41 && reg_in(id, 11) == 62 && reg_in(id, 14) == 0,
              "and answers ISP's identity read as its bootloader: 11 the version, 14 zero");
        check(isp.ask(kJump) == kJump, "the jump, in the bootloader, is an init");
        check(isp.ask(kReg11) == kReg11Is62, "register 11");
        check(isp.ask(kStatus) == kReadsInterrupted, "0xEE10 reads 0x1F: interrupted (:2636)");
        check(isp.ask(kPackets) == kPacketsAre2, "1991 reads 2 (:2640)");

        // OK in ISP's box: no MD5, no init; the section's blocks from 0,
        // with the data from the resume offset (:2910, 1096-1121).
        isp.ask(kQuiet);
        check(isp.ask(kSection1) == kSection1, "register 12 = 1");
        check_eq(send_blocks(isp, 1, file, 256, 256), 2, "the rest, from address 0");
        check(isp.ask(kEnd) == kEnd, "the end");
        check_eq(dev.frames_not_isp(), 0, "a resume from address 0 with no 0x1F is ISP's");
        check_eq(dev.status, 0, "and the flash is no longer interrupted");
    }

    void test_a_first_thread_resume()
    {
        section("synthetic bootloader: flash_a_tstat's resume, from register 1991");
        FakeBootloader d = device(88);
        d.mode   = FakeBootloader::Mode::Bootloader;
        d.status = kStatusInterrupted2;
        d.update_registers[1993] = 0x1234;
        d.update_registers[1994] = 0x5678;
        d.update_registers[1995] = 0x9ABC;
        d.update_registers[1996] = 0xDEF0;
        d.packets_override = 3;
        FakeRtuBus bus = bus_with(d);
        Isp isp{ bus };
        const Bytes file = file_of(5 * 128, 8);

        const Bytes s = isp.ask(kStatus);
        check(s.size() == 7 && reg_in(s, 0) == 0x40, "0xEE10 reads 0x40 (:861)");
        const Bytes md5 = isp.ask(kMd5);
        check(md5.size() == 13 && reg_in(md5, 0) == 0x1234 && reg_in(md5, 3) == 0xDEF0, "the MD5 kept, four words (:868)");
        const Bytes p = isp.ask(kPackets);
        check(p.size() == 7 && reg_in(p, 0) == 3, "1991 reads 3 (:831-836)");
        check(isp.ask(kStart) == kStart, "0x1F, and no erase (:963-968)");

        // An ESP32 resumes at 3 * 128 (:976-981).
        check_eq(send_blocks(isp, 1, file, 384, 256, 384), 2, "the blocks from 0x0180");
        check(isp.ask(kEnd) == kEnd, "the end");
        check_eq(bus.devices[0].frames_not_isp(), 0, "a resume from 0x0180 with no erase is ISP's");
    }

    void test_only_a_resumes_first_block_starts_past_0()
    {
        section("synthetic bootloader: only a resume's first block starts past 0, where 1991 says");
        // Back in its bootloader after a flash cut short, 1991 reading 2.
        const auto resumed = [](uint16_t product) {
            FakeBootloader d = device(product);
            d.mode   = FakeBootloader::Mode::Bootloader;
            d.status = kStatusInterrupted;
            d.packets_override = 2;
            return d;
        };
        const Bytes file = file_of(4 * 128, 6);
        {
            FakeRtuBus bus = bus_with(resumed(88));
            Isp isp{ bus };
            check_eq(send_blocks(isp, 1, file, 256, 128, 0x0100), 1, "flash_a_tstat's first block at 2 * 128 is taken");
            check_eq(bus.devices[0].frames_not_isp(), 0, "and is ISP's (:885, 976-981)");
            isp.ask(kSection2);
            send_blocks(isp, 1, file, 0, 128, 0x0100);
            check_eq(bus.devices[0].frames_not_isp(), 1,
                     "a later section's first block at 0x0100 is judged: ISP starts each from 0 (:1070-1096)");
        }
        {
            FakeRtuBus bus = bus_with(resumed(88));
            Isp isp{ bus };
            send_blocks(isp, 1, file, 128, 128, 0x0080);
            check_eq(bus.devices[0].frames_not_isp(), 1, "a resume's first block neither at 0 nor at 1991 * 128 is judged");
        }
        {
            FakeRtuBus bus = bus_with(resumed(74));
            Isp isp{ bus };
            isp.ask(kSection1);
            send_blocks(isp, 1, file, 256, 256);
            isp.ask(kSection2);
            send_blocks(isp, 1, file, 0, 128);
            check_eq(bus.devices[0].frames_not_isp(), 0, "the ARM thread's resume, each section from 0, is ISP's (:2910, 1096-1121)");
        }
    }

    // --- what ISP would not send ------------------------------------------

    void test_frames_isp_would_not_send_are_judged()
    {
        section("synthetic bootloader: frames ISP would not send");
        FakeBootloader d = device(74);
        d.mode = FakeBootloader::Mode::Bootloader;
        FakeRtuBus bus = bus_with(d);
        Isp isp{ bus };
        const Bytes file = file_of(4 * 128, 6);
        FakeBootloader& dev = bus.devices[0];

        check(isp.ask(kErase) == kErase, "an erase before any 0x7F is still echoed");
        check_eq(dev.frames_not_isp(), 1, "but judged");

        check(isp.ask(block_frame(1, 0, file, 0)) == kBlockReply0, "a block before 0x1F is taken");
        check_eq(dev.frames_not_isp(), 2, "and judged");

        isp.ask(kStart);
        isp.ask(block_frame(1, 0x0100, file, 256));
        check_eq(dev.frames_not_isp(), 3, "a block at 0x0100 after one at 0 is judged");

        Bytes standard = { 0x01, 0x10, 0x00, 0x00, 0x00, 0x40, 0x80 };
        standard.insert(standard.end(), file.begin(), file.begin() + 128);
        add_line_crc(standard);
        check(isp.ask(standard).empty(), "a block framed as standard Modbus, 00 40, is not answered");
        check_eq(dev.frames_not_isp(), 4, "and judged");

        const Bytes read10 = { 0x01, 0x03, 0x00, 0x00, 0x00, 0x0A, 0xC5, 0xCD };
        check_eq((long)isp.ask(read10).size(), 25, "a read of 10 registers is answered");
        check_eq(dev.frames_not_isp(), 5, "but judged: ISP reads 40 or 18");

        const Bytes w55 = { 0x01, 0x06, 0x00, 0x10, 0x00, 0x55, 0x48, 0x30 };
        check(isp.ask(w55) == w55, "16 = 0x55 is echoed");
        check_eq(dev.frames_not_isp(), 6, "and judged");

        const Bytes quiet1 = { 0x01, 0x06, 0x00, 0x10, 0x04, 0x55, 0x4A, 0xF0 };
        isp.ask(quiet1);
        check_eq(dev.frames_not_isp(), 7, "0x0455 to the device's own id is judged");

        const Bytes fc4 = { 0x01, 0x04, 0x00, 0x00, 0x00, 0x01, 0x31, 0xCA };
        const Bytes exception = { 0x01, 0x84, 0x01, 0x82, 0xC0 };
        check(isp.ask(fc4) == exception, "function 4 gets exception 1");
        check_eq(dev.frames_not_isp(), 8, "and is judged");

        Bytes bad = kStatus;
        bad.back() ^= 0x01;
        check(isp.ask(bad).empty(), "a frame with a bad CRC is not heard");
        check_eq(bus.bad_crc_frames, 1, "and is counted by the bus");
        check_eq(bus.frames_not_isp(), 9, "the bus's count has the device's and its own");

        const std::vector<std::string> reasons = dev.not_isp_reasons();
        check(reasons.size() == 8 && reasons[0].find("an erase before any 0x7F") != std::string::npos,
              "each with its reason");

        FakeRtuBus app = bus_with(device(74));
        Isp other{ app };
        check(other.ask(block_frame(1, 0, file, 0)).empty(), "a running application does not answer a block");
        check_eq(app.devices[0].frames_not_isp(), 1, "which is judged");
        check(app.devices[0].blocks.empty(), "and not taken");
    }

    // --- faults ------------------------------------------------------------

    void test_faults_on_reads_and_writes()
    {
        section("synthetic bootloader: faults on reads and writes");
        using Action = ModbusFault::Action;
        using Kind   = ModbusFault::Kind;

        {
            FakeBootloader d = device(9);
            ModbusFault x;
            x.kind = Kind::Read;
            x.reg = 0;
            x.action = Action::Deaf;
            x.times = 6;
            d.faults.push_back(x);
            FakeRtuBus bus = bus_with(d);
            Isp isp{ bus };
            int silent = 0;
            for (int i = 0; i < 6; i++)
                silent += isp.ask(kRead40, 2060).empty() ? 1 : 0;
            check_eq(silent, 6, "the identity read is lost six times, ISP's tries (:427-448)");
            check_eq((long)isp.ask(kRead40, 2060).size(), 85, "and answered the seventh");
            check(bus.devices[0].heard[0].what.find("(lost)") != std::string::npos, "the lost ones kept as lost");
        }
        {
            FakeBootloader d = device(9);
            d.mode = FakeBootloader::Mode::Bootloader;
            ModbusFault x;
            x.kind = Kind::Read;
            x.reg = kRegStatus;
            x.action = Action::Exception;
            d.faults.push_back(x);
            FakeRtuBus bus = bus_with(d);
            Isp isp{ bus };
            const Bytes exception = { 0x01, 0x83, 0x02, 0xC0, 0xF1 };
            check(isp.ask(kStatus) == exception, "an exception: 01 83 02, five bytes, which ISP takes as a bad reply");
            check(isp.ask(kStatus) == kReadsIdle, "once");
        }
        {
            FakeBootloader d = device(9);
            d.mode = FakeBootloader::Mode::Bootloader;
            ModbusFault x;
            x.kind = Kind::Write;
            x.reg = kRegCommand;
            x.value = kCmdStart;
            x.action = Action::Silence;
            d.faults.push_back(x);
            ModbusFault g = x;
            g.action = Action::Garble;
            g.value = kCmdEnd;
            d.faults.push_back(g);
            FakeRtuBus bus = bus_with(d);
            Isp isp{ bus };
            check(isp.ask(kStart).empty(), "0x1F's echo is lost");
            check_eq(bus.devices[0].status, kStatusInterrupted, "though the device took it");
            const Bytes r = isp.ask(kEnd);
            check(r.size() == 8 && !line_crc_ok(r), "16=1's echo arrives with a bad CRC");
        }
        {
            FakeBootloader d = device(9);
            d.mode = FakeBootloader::Mode::Bootloader;
            ModbusFault v;
            v.kind = Kind::Write;
            v.action = Action::WrongValue;
            ModbusFault n = v;
            n.kind = Kind::Read;
            ModbusFault i = v;
            i.kind = Kind::Read;
            i.action = Action::WrongId;
            d.faults = { v, n, i };   // the first that matches applies: n, then i
            FakeRtuBus bus = bus_with(d);
            Isp isp{ bus };
            const Bytes w = isp.ask(kStart);
            check(w.size() == 8 && w[4] == 0x01 && w[5] == 0x1F && line_crc_ok(w),
                  "a wrong echo: the value's high byte, which ISP compares over TCP too (common.cpp:3002)");
            const Bytes r = isp.ask(kReg11);
            check(r.size() == 5 && r[2] == 0 && line_crc_ok(r), "a read two bytes short");
            const Bytes r2 = isp.ask(kReg11);
            check(r2.size() == 7 && r2[0] == 0x02 && line_crc_ok(r2), "a reply from id 2");
        }
        {
            FakeBootloader d = device(9);
            d.mode = FakeBootloader::Mode::Bootloader;
            ModbusFault x;
            x.kind = Kind::Read;
            x.reg = kRegBoot11;
            x.action = Action::Silence;
            x.skip = 1;
            x.times = 2;
            ModbusFault late;
            late.kind = Kind::Write;
            late.action = Action::Late;
            late.delay_ms = 600;
            d.faults = { x, late };
            FakeRtuBus bus = bus_with(d);
            Isp isp{ bus };
            check(isp.ask(kReg11) == kReg11Is62, "let through once");
            check(isp.ask(kReg11).empty() && isp.ask(kReg11).empty(), "then silent twice");
            check(isp.ask(kReg11) == kReg11Is62, "then answered again");
            check(isp.ask(kStart).empty(), "a reply 600 ms late misses ISP's 520");
            isp.wait(100);
            check(isp.ask(kReg11) == kReg11Is62, "and, arrived by then, is purged before the next request, as PurgeComm does");
        }
    }

    void test_faults_on_blocks()
    {
        section("synthetic bootloader: faults on blocks");
        using Action = ModbusFault::Action;
        using Kind   = ModbusFault::Kind;
        const Bytes file = file_of(3 * 128, 12);

        {
            FakeBootloader d = device(9);
            d.mode = FakeBootloader::Mode::Bootloader;
            ModbusFault x;
            x.kind = Kind::Block;
            x.address = 0x80;
            x.action = Action::Silence;
            ModbusFault deaf = x;
            deaf.address = 0x100;
            deaf.action = Action::Deaf;
            d.faults = { x, deaf };
            FakeRtuBus bus = bus_with(d);
            Isp isp{ bus };
            isp.ask(kStart);
            check(isp.ask(block_frame(1, 0, file, 0)) == kBlockReply0, "block 0");
            check(isp.ask(block_frame(1, 0x80, file, 128)).empty(), "block 0x80's reply is lost");
            check_eq((long)bus.devices[0].blocks.size(), 2, "though the block was taken");
            check(isp.ask(block_frame(1, 0x80, file, 128)) == kBlockReply80, "sent again, it is answered (:1003-1021)");
            check(isp.ask(block_frame(1, 0x100, file, 256)).empty(), "block 0x100 is lost");
            check_eq((long)bus.devices[0].blocks.size(), 3, "and not taken");
            isp.ask(block_frame(1, 0x100, file, 256));
            const FakeBootloader& dev = bus.devices[0];
            check_eq(dev.packets(), 3, "1991 counts a block sent twice once");
            check(dev.image() == file, "the flash holds the file");
            check_eq(dev.frames_not_isp(), 0, "a block sent again is ISP's");
        }
        {
            FakeBootloader d = device(9);
            d.mode = FakeBootloader::Mode::Bootloader;
            ModbusFault x;
            x.kind = Kind::Block;
            x.action = Action::WrongValue;
            d.faults = { x };
            FakeRtuBus bus = bus_with(d);
            Isp isp{ bus };
            isp.ask(kStart);
            const Bytes standard = { 0x01, 0x10, 0x00, 0x00, 0x00, 0x40, 0xC1, 0xF9 };
            check(isp.ask(block_frame(1, 0, file, 0)) == standard,
                  "a block's reply with the standard count 00 40, which ISP refuses (common.cpp:4305-4312)");
        }
        {
            FakeBootloader d = device(9);
            d.cut_off_after_blocks = 1;
            FakeRtuBus bus = bus_with(d);
            Isp isp{ bus };
            isp.ask(kJump);
            isp.ask(kJump);
            isp.ask(kErase);
            isp.ask(kStart);
            check(isp.ask(block_frame(1, 0, file, 0)) == kBlockReply0, "the first block is answered");
            check(isp.ask(block_frame(1, 0x80, file, 128)).empty(), "the second is not");
            check(isp.ask(kWake).empty(), "nor the wake-up read to 255 (:1013)");
            check(isp.ask(block_frame(1, 0x80, file, 128)).empty(), "nor the second again");
            FakeBootloader& dev = bus.devices[0];
            check_eq(dev.status, kStatusInterrupted, "0xEE10 stays 0x1F");
            dev.power_on(isp.now);
            const Bytes p = isp.ask(kPackets);
            check(p.size() == 7 && reg_in(p, 0) == 1, "back on, 1991 reads 1");
            check(isp.ask(kStatus) == kReadsInterrupted, "and 0xEE10 0x1F");
        }
    }

    void test_timing_and_rates()
    {
        section("synthetic bootloader: silent while it restarts and erases; its rates");
        {
            FakeBootloader d = device(9);
            d.jump_ms  = 2000;
            d.erase_ms = 7000;
            FakeRtuBus bus = bus_with(d);
            Isp isp{ bus };
            check(isp.ask(kJump) == kJump, "the jump is echoed");
            check(isp.ask(kReg11).empty(), "then nothing while it restarts");
            isp.wait(2000);
            check(isp.ask(kReg11) == kReg11Is62, "then its bootloader answers");
            check(isp.ask(kErase) == kErase, "the erase is echoed");
            check(isp.ask(kStart).empty(), "then nothing while it erases");
            isp.wait(7000);
            check(isp.ask(kStart) == kStart, "then 0x1F is, after ISP's 7 s (:958)");
            check(bus.devices[0].heard[1].what.find("(busy)") != std::string::npos, "the frames missed kept as busy");
        }
        {
            // A TStat10 with an old bootloader: its application at 76800,
            // its bootloader at 57600 (:1959-1981, 2530-2538).
            FakeBootloader d = device(10);
            d.app_baud  = 76800;
            d.boot_baud = 57600;
            FakeRtuBus bus = bus_with(d);
            Isp isp{ bus };
            bus.set_rate(76800);
            check_eq((long)isp.ask(kRead18).size(), 41, "the application answers at 76800");
            check(isp.ask(kJump) == kJump, "and echoes the jump");
            check(isp.ask(kReg11).empty(), "the bootloader does not answer at 76800");
            bus.set_rate(57600);
            check(isp.ask(kReg11) == kReg11Is62, "but does at 57600");
        }
    }

    void test_id_255_and_several_devices()
    {
        section("synthetic bootloader: id 255, and several devices on one line");
        {
            FakeBootloader d = device(9);
            d.set_identity(5, 1, 9, 538, 62);
            FakeRtuBus bus = bus_with(d);
            Isp isp{ bus };
            const Bytes read255 = { 0xFF, 0x03, 0x00, 0x00, 0x00, 0x28, 0x50, 0x0A };
            check(isp.ask(read255).empty(), "by default a device does not answer 255");
            check_eq((long)bus.devices[0].heard.size(), 1, "though it heard it");
            bus.devices[0].answers_id_255 = true;
            const Bytes r = isp.ask(read255);
            check(r.size() == 85 && r[0] == 0xFF && reg_in(r, 6) == 5,
                  "one that does answers as 255, with its own id in register 6 (:493)");
        }
        {
            FakeRtuBus bus;
            bus.devices = { device(9), device(74) };
            bus.devices[1].set_identity(2, 2, 74, 538, 62);
            Isp isp{ bus };
            const Bytes read2 = { 0x02, 0x03, 0x00, 0x00, 0x00, 0x28, 0x45, 0xE7 };
            const Bytes jump2 = { 0x02, 0x06, 0x00, 0x10, 0x00, 0x7F, 0xC9, 0xDC };
            const Bytes r = isp.ask(read2);
            check(r.size() == 85 && r[0] == 0x02 && reg_in(r, 7) == 74, "id 2 answers for itself");
            check(isp.ask(jump2) == jump2, "and jumps");
            check(bus.device(2)->mode == FakeBootloader::Mode::Bootloader, "id 2 is its bootloader");
            check(bus.device(1)->mode == FakeBootloader::Mode::Application, "id 1 is still running");
            check(bus.device(1)->heard.empty(), "and heard nothing for id 2");
        }
        {
            FakeRtuBus bus;
            bus.devices = { device(9), device(9) };
            Isp isp{ bus };
            check_eq((long)isp.ask(kRead18).size(), 82, "two devices on one id answer on top of each other");
        }
    }

    void test_the_update_registers()
    {
        section("synthetic bootloader: registers 1992-1999, 23 and 0xEE10");
        FakeBootloader d = device(88);
        d.boot_registers[kRegEspStatus] = 0x51;
        FakeRtuBus bus = bus_with(d);
        Isp isp{ bus };

        // An ESP32's resume check, before the jump (:1434-1478).
        check(isp.ask(kW1995) == kW1995, "the application echoes 1995");
        const Bytes r = isp.ask(kRead1994);
        check(r.size() == 17 && reg_in(r, 1) == 0xABCD, "and reads it back at 1994's second word");
        isp.ask(kJump);
        const Bytes m = isp.ask(kMd5);
        check(m.size() == 13 && reg_in(m, 2) == 0xABCD, "its bootloader reads the same register (:868)");
        const Bytes esp = isp.ask(kEsp23);
        check(esp.size() == 7 && reg_in(esp, 0) == 0x51, "register 23, an ESP32's end, as set (:1770-1803)");
        check_eq(bus.devices[0].frames_not_isp(), 0, "all of it ISP's");
    }

    void test_choices()
    {
        section("synthetic bootloader: the choices");
        const Bytes file = file_of(2 * 128, 13);
        {
            FakeBootloader d = device(9);
            d.mode = FakeBootloader::Mode::Bootloader;
            d.end_restarts = true;
            d.restart_ms   = 1000;
            FakeRtuBus bus = bus_with(d);
            Isp isp{ bus };
            check(isp.ask(kEnd) == kEnd, "16=1 before any block is echoed");
            check(bus.devices[0].mode == FakeBootloader::Mode::Bootloader, "and it stays, even when an end restarts");
            isp.ask(kStart);
            send_blocks(isp, 1, file, 0, file.size());
            check(isp.ask(kEnd) == kEnd, "16=1 after the blocks is echoed");
            check(bus.devices[0].mode == FakeBootloader::Mode::Application, "and it restarts");
            check(isp.ask(kReg11).empty(), "silent while it does");
            isp.wait(1000);
            const Bytes r = isp.ask(kReg11);
            check(r.size() == 7 && reg_in(r, 0) == 1, "then its application answers, register 11 at 1");
        }
        {
            FakeBootloader d = device(9);
            d.mode = FakeBootloader::Mode::Bootloader;
            FakeRtuBus bus = bus_with(d);
            Isp isp{ bus };
            isp.ask(kJump);
            isp.ask(kErase);
            isp.ask(kStart);
            send_blocks(isp, 1, file, 0, file.size());
            isp.ask(kEnd);
            isp.ask(kJump);
            isp.ask(kErase);
            isp.ask(kStart);
            send_blocks(isp, 1, file, 0, 128);
            const Bytes image = bus.devices[0].image();
            check_eq((long)image.size(), 128, "an erase clears its bank: only the block after it is left");
            check_eq(bus.devices[0].packets(), 1, "and 1991 counts from the erase");
        }
    }
}

int run_fake_bootloader_tests()
{
    test_the_frames_are_modbus();
    test_isp_flashes_a_data_hex();
    test_isp_flashes_a_bin();
    test_a_bin_past_64k_wraps();
    test_isp_flashes_two_sections();
    test_isp_flashes_an_arm_hex();
    test_a_cut_arm_flash_resumes();
    test_a_first_thread_resume();
    test_only_a_resumes_first_block_starts_past_0();
    test_frames_isp_would_not_send_are_judged();
    test_faults_on_reads_and_writes();
    test_faults_on_blocks();
    test_timing_and_rates();
    test_id_255_and_several_devices();
    test_the_update_registers();
    test_choices();
    return 0;
}
