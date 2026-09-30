// Tests for the synthetic TFTP bootloader (fake_tftp_bootloader.h).
//
// Each test plays ISP's side of a flash over the network as ISP's source has
// it (ISP\TFTPServer.cpp, ISP\MySocket.cpp): the EE 10 and the Modbus read
// on TCP, then "Temcocontrols", the blocks and FLASH DONE on UDP, from ISP's
// port 10001. The packets are built here from ISP's layouts, each with the
// line of ISP it comes from, and the replies are checked byte by byte
// against what ISP reads of them.

#include "../testing/check.h"
#include "../testing/fake_tftp_bootloader.h"

#include <string>

namespace
{
    using namespace t5000::testing;
    using Datagram = FakeTftpBootloader::Datagram;

    // TFTPServer.cpp:1093-1095: EE 10 and 62 zeros.
    Bytes ee10()
    {
        Bytes b(64, 0);
        b[0] = 0xEE;
        b[1] = 0x10;
        return b;
    }

    // TFTPServer.cpp:2092: registers 0-99 of unit 255, the first request
    // on the Modbus DLL's socket.
    const Bytes kModbusRead = { 0x00, 0x01, 0x00, 0x00, 0x00, 0x06, 0xFF, 0x03, 0x00, 0x00, 0x00, 0x64 };

    // TFTPServer.cpp:511-562: "Temcocontrols", the IP ISP assigns, the mask,
    // zeros, the IP the device gave, zeros to 45 bytes.
    Bytes hello(std::array<uint8_t, 4> assigned = { 0, 0, 0, 0 }, std::array<uint8_t, 4> device = { 0, 0, 0, 0 })
    {
        Bytes b(45, 0);
        const char* s = "Temcocontrols";
        for (int i = 0; i < 13; i++)
            b[i] = (uint8_t)s[i];
        for (int i = 0; i < 4; i++)
        {
            b[13 + i] = assigned[i];
            b[25 + i] = device[i];
        }
        b[17] = 0xFF;
        b[18] = 0xFF;
        b[19] = 0xFF;
        return b;
    }

    // TFTPServer.cpp:357-375: 00 03, the block number big-endian, up to 512
    // bytes of the file.
    Bytes data(int k, const Bytes& file)
    {
        Bytes b = { 0x00, 0x03, (uint8_t)(k >> 8), (uint8_t)(k & 0xFF) };
        const size_t at = (size_t)(k - 1) * 512;
        for (size_t i = at; i < file.size() && i < at + 512; i++)
            b.push_back(file[i]);
        return b;
    }

    const Bytes kFlashDone = { 'F', 'L', 'A', 'S', 'H', ' ', 'D', 'O', 'N', 'E' };   // :1082-1083
    const Bytes kDone      = { 0x00, 0x04, 0xFF, 0xFF };                             // MySocket.cpp:302-313

    Bytes ack(int k) { return { 0x00, 0x04, (uint8_t)(k >> 8), (uint8_t)(k & 0xFF) }; }

    Bytes file_of(size_t size, uint8_t seed)
    {
        Bytes b(size);
        for (size_t i = 0; i < size; i++)
            b[i] = (uint8_t)(i * 3 + seed);
        return b;
    }

    // The 31-byte reply as ISP reads it (MySocket.cpp:104-115, 200-212).
    bool is_bootloader_reply(const Datagram& d, const std::array<uint8_t, 4>& ip, const std::string& name)
    {
        const Bytes& b = d.bytes;
        if (b.size() != 31 || std::string(b.begin(), b.begin() + 11) != "ReceiveDHCP")
            return false;
        for (int i = 0; i < 4; i++)
            if (b[11 + i] != ip[i])
                return false;
        const std::string got((const char*)&b[15]);   // up to the NUL, as ISP's strlen
        return got == name && b[26] == 0;
    }

    struct Isp
    {
        FakeTftpBootloader& device;
        int64_t now = 0;

        std::vector<Datagram> send(const Bytes& b, int wait_ms = 1000)
        {
            auto r = device.udp(b, kLocalUdpPort, now);
            now += wait_ms;
            return r;
        }

        Bytes tcp(const Bytes& b)
        {
            const Bytes r = device.tcp(b, now);
            now += 50;
            return r;
        }

        // Every block of the file, each once: how many were ACKed as ISP
        // wants (MySocket.cpp:286-296).
        int blocks(const Bytes& file)
        {
            int good = 0;
            const int n = (int)((file.size() + 511) / 512);
            for (int k = 1; k <= n; k++)
            {
                const auto r = send(data(k, file), 2);
                good += r.size() == 1 && r[0].bytes == ack(k) && r[0].port == kLocalUdpPort ? 1 : 0;
            }
            return good;
        }
    };

    const std::array<uint8_t, 4> kLoopback = { 127, 0, 0, 1 };

    void test_isp_flashes_a_running_device()
    {
        section("synthetic TFTP bootloader: a device whose application is running");
        FakeTftpBootloader d;
        d.jump_ms = 7000;
        Isp isp{ d };
        const Bytes file = file_of(2 * 512 + 100, 4);

        // The read before a flash (TFTPServer.cpp:2078-2141).
        const Bytes m = isp.tcp(kModbusRead);
        if (require(m.size() == 209, "the Modbus read is answered in one piece of 209 bytes"))
        {
            check(m[6] == 0xFF && m[7] == 0x03 && m[8] == 200, "unit 255, fc 3, 200 bytes, as ISP checks them");
            check((m[9 + 14] << 8 | m[10 + 14]) == 74, "register 7, the product");
            check((m[9 + 22] << 8 | m[10 + 22]) == 62 && (m[9 + 28] << 8 | m[10 + 28]) == 62, "11 and 14, the bootloader");
        }

        // State 0: EE 10 on TCP, then "Temcocontrols" (TFTPServer.cpp:1140-1194).
        const Bytes r40 = isp.tcp(ee10());
        if (require(r40.size() == 40, "EE 10 is answered with 40 bytes"))
        {
            check(r40[0] == 0x65 && r40[1] == 0x00, "65 00 (MySocket.cpp:86-92)");
            check(r40[16] == 127 && r40[18] == 0 && r40[20] == 0 && r40[22] == 1, "its IP at 16, 18, 20 and 22");
        }
        check(isp.send(hello({ 127, 0, 0, 1 })).empty(), "restarting, its bootloader does not answer yet");

        // State 2, after ISP's 7 s (:1236-1247): to the device's IP.
        isp.now = 8000;
        const auto r = isp.send(hello({ 127, 0, 0, 1 }, kLoopback));
        check(r.size() == 1 && is_bootloader_reply(r[0], kLoopback, "MINIPANEL"),
              "its bootloader answers: ReceiveDHCP, its IP, its name, NUL-ended");
        check(r.size() == 1 && r[0].port == kLocalUdpPort, "to the port ISP sent from");

        check_eq(isp.blocks(file), 3, "three blocks, each ACKed with its number");
        const auto done = isp.send(kFlashDone);
        check(done.size() == 1 && done[0].bytes == kDone, "FLASH DONE is answered 00 04 FF FF");

        check_eq(d.frames_not_isp(), 0, "nothing sent was outside ISP's flash");
        check(d.image() == file, "the device holds the file");
        check(d.finished, "and says it is done");
    }

    void test_isp_flashes_a_device_in_its_bootloader()
    {
        section("synthetic TFTP bootloader: a device already in its bootloader");
        FakeTftpBootloader d;
        d.mode = FakeTftpBootloader::Mode::Bootloader;
        d.name = "PID10";
        Isp isp{ d };
        const Bytes file = file_of(2 * 512, 9);

        check(isp.tcp(ee10()).empty(), "a bootloader answers no EE 10");
        const auto first = isp.send(hello());
        check(first.size() == 1 && is_bootloader_reply(first[0], kLoopback, "PID10"), "it answers state 0's Temcocontrols");
        const auto second = isp.send(hello({ 0, 0, 0, 0 }, kLoopback));
        check(second.size() == 1 && is_bootloader_reply(second[0], kLoopback, "PID10"),
              "and state 2's, which ISP must have (MySocket.cpp:198)");
        check_eq(isp.blocks(file), 2, "two full blocks, and nothing after them (TFTPServer.cpp:1523-1536)");
        check(isp.send(kFlashDone).size() == 1, "done");
        check_eq(d.frames_not_isp(), 0, "nothing sent was outside ISP's flash");
        check(d.image() == file, "the device holds the file");
    }

    void test_datagrams_isp_would_not_send()
    {
        section("synthetic TFTP bootloader: what ISP would not send");
        FakeTftpBootloader d;
        d.mode = FakeTftpBootloader::Mode::Bootloader;
        Isp isp{ d };
        const Bytes file = file_of(3 * 512, 1);
        int n = 0;

        check(!isp.send(data(1, file)).empty(), "a block before the bootloader's reply is still ACKed");
        check_eq(d.frames_not_isp(), ++n, "but judged");

        Bytes masked = hello();
        masked[20] = 0xFF;
        isp.send(masked);
        check_eq(d.frames_not_isp(), ++n, "a mask other than FF FF FF 00 is judged");
        Bytes short_hello = hello();
        short_hello.pop_back();
        isp.send(short_hello);
        check_eq(d.frames_not_isp(), ++n, "a Temcocontrols of 44 bytes is judged");

        isp.send(data(3, file));
        check_eq(d.frames_not_isp(), ++n, "block 3 after block 1 is judged");
        isp.send(data(1, file));
        check_eq(d.frames_not_isp(), ++n, "and block 1 after block 3");

        Bytes big = data(4, file);
        big.resize(4 + 513, 0);
        isp.send(big);
        check_eq(d.frames_not_isp(), ++n, "a block of 513 bytes is judged");

        FakeTftpBootloader e;
        e.mode = FakeTftpBootloader::Mode::Bootloader;
        Isp other{ e };
        other.send(hello());
        check_eq(e.frames_not_isp(), 0, "a clean Temcocontrols is ISP's");
        other.send(data(2, file));
        check_eq(e.frames_not_isp(), 1, "a first block numbered 2 is judged");
        FakeTftpBootloader f;
        f.mode = FakeTftpBootloader::Mode::Bootloader;
        Isp third{ f };
        third.send(hello());
        third.send(kFlashDone);
        check_eq(f.frames_not_isp(), 1, "FLASH DONE before any block is judged");
        third.send(data(1, file_of(100, 1)));
        check_eq(f.frames_not_isp(), 2, "a block after FLASH DONE is judged");

        FakeTftpBootloader g;
        g.mode = FakeTftpBootloader::Mode::Bootloader;
        Isp fourth{ g };
        fourth.send(hello());
        fourth.send(data(1, file_of(100, 1)));
        fourth.send(data(2, file_of(700, 1)));
        check_eq(g.frames_not_isp(), 1, "a block after a short one is judged");
        Bytes done_nul = kFlashDone;
        done_nul.push_back(0);
        check(fourth.send(done_nul).empty(), "FLASH DONE with a NUL is not FLASH DONE");
        check_eq(g.frames_not_isp(), 2, "and is judged");
        Bytes ee = ee10();
        ee[63] = 1;
        fourth.tcp(ee);
        check_eq(g.frames_not_isp(), 3, "an EE 10 whose tail is not zero is judged");
        Bytes read1 = kModbusRead;
        read1[6] = 1;
        fourth.tcp(read1);
        check_eq(g.frames_not_isp(), 4, "a Modbus read of unit 1 is judged");
        fourth.tcp({ 1, 2, 3 });
        check_eq(g.frames_not_isp(), 5, "and three bytes on TCP");
        check(g.not_isp_reasons().size() == 5, "each with its reason");
    }

    void test_faults_in_the_handshake()
    {
        section("synthetic TFTP bootloader: faults in the handshake");
        {
            FakeTftpBootloader d;
            d.mode = FakeTftpBootloader::Mode::Bootloader;
            d.hello_silent = 2;
            Isp isp{ d };
            check(isp.send(hello()).empty() && isp.send(hello()).empty(), "two Temcocontrols go unanswered");
            check_eq((long)isp.send(hello()).size(), 1, "the third is answered");
        }
        {
            FakeTftpBootloader d;
            d.mode = FakeTftpBootloader::Mode::Bootloader;
            d.answers_first_hello_only = true;
            Isp isp{ d };
            check_eq((long)isp.send(hello()).size(), 1, "the first is answered");
            check(isp.send(hello({ 0, 0, 0, 0 }, kLoopback)).empty(), "state 2's is not, which ISP fails on (TFTPServer.cpp:1306-1336)");
        }
        {
            FakeTftpBootloader d;
            d.mode = FakeTftpBootloader::Mode::Bootloader;
            d.reply_length = 30;
            d.name = "TSTAT8";
            d.reply_ip = { 10, 0, 0, 9 };
            d.reply_ip_set = true;
            Isp isp{ d };
            const auto r = isp.send(hello());
            check(r.size() == 1 && r[0].bytes.size() == 30, "a reply of 30 bytes, which ISP ignores (MySocket.cpp:104)");
            check(r.size() == 1 && r[0].bytes[11] == 10 && r[0].bytes[14] == 9, "with another IP, which ISP would adopt");
            check(r.size() == 1 && r[0].bytes[15] == 'T' && r[0].bytes[20] == '8', "and another name");
        }
        {
            FakeTftpBootloader d;
            d.runtime_silent = true;
            Isp isp{ d };
            check(isp.tcp(ee10()).empty(), "a running device that does not answer EE 10");
            check(d.mode == FakeTftpBootloader::Mode::Application, "and stays running");
            d.runtime_silent = false;
            d.runtime_reply_length = 39;
            check_eq((long)isp.tcp(ee10()).size(), 39, "a runtime reply of 39 bytes, which ISP ignores (MySocket.cpp:86)");
        }
        {
            FakeTftpBootloader d;
            d.mode = FakeTftpBootloader::Mode::Bootloader;
            d.reply_to_10001 = true;
            d.modbus_in_bootloader = false;
            const auto r = d.udp(hello(), 50000, 0);
            check(r.size() == 1 && r[0].port == kLocalUdpPort, "reply_to_10001 sends to 10001, whatever the sender's port");
            const auto s = FakeTftpBootloader().udp(hello(), 50000, 0);
            check(s.empty(), "a running device does not answer Temcocontrols");
            check(d.tcp(kModbusRead, 0).empty(), "and a bootloader with no Modbus does not answer the read");
            FakeTftpBootloader e;
            e.mode = FakeTftpBootloader::Mode::Bootloader;
            const auto t = e.udp(hello(), 50000, 0);
            check(t.size() == 1 && t[0].port == 50000, "by default it replies to the sender's port");
        }
    }

    void test_faults_on_blocks_and_the_end()
    {
        section("synthetic TFTP bootloader: faults on blocks and the end");
        const Bytes file = file_of(3 * 512 + 10, 2);
        {
            FakeTftpBootloader d;
            d.mode = FakeTftpBootloader::Mode::Bootloader;
            FakeTftpBootloader::AckFault drop;
            drop.block = 2;
            drop.drop  = 9;
            FakeTftpBootloader::AckFault wrong;
            wrong.block  = 3;
            wrong.ack_as = 2;
            wrong.delay_ms = 50;
            d.ack_faults = { drop, wrong };
            d.first_block_ms = 3000;
            Isp isp{ d };
            isp.send(hello());
            const auto one = isp.send(data(1, file), 2);
            check(one.size() == 1 && one[0].delay_ms == 3000, "block 1's ACK comes 3 s late, the erase");
            int silent = 0;
            for (int i = 0; i < 9; i++)
                silent += isp.send(data(2, file), 2).empty() ? 1 : 0;
            check_eq(silent, 9, "block 2's first nine sends go unACKed");
            const auto tenth = isp.send(data(2, file), 2);
            check(tenth.size() == 1 && tenth[0].bytes == ack(2), "the tenth is ACKed, the last ISP takes (TFTPServer.cpp:1548-1605)");
            check_eq(d.sends[2], 10, "block 2 came ten times");
            const auto three = isp.send(data(3, file), 2);
            check(three.size() == 1 && three[0].bytes == ack(2) && three[0].delay_ms == 50, "block 3 is ACKed as block 2, late");
            check_eq(d.frames_not_isp(), 0, "blocks sent again are ISP's");
            isp.send(data(3, file), 2);
            isp.send(data(4, file), 2);
            check(d.image() == file, "the device holds the file, each block once");
        }
        {
            FakeTftpBootloader d;
            d.mode = FakeTftpBootloader::Mode::Bootloader;
            d.cut_off_after_blocks = 2;
            Isp isp{ d };
            isp.send(hello());
            check_eq(isp.blocks(file), 2, "two blocks are ACKed");
            check(d.cut_off(), "then it is cut off");
            check(isp.send(kFlashDone).empty() && isp.send(hello()).empty(), "and answers nothing");
            d.power_on(isp.now);
            d.cut_off_after_blocks = -1;
            check_eq((long)isp.send(hello()).size(), 1, "back on, it answers a new handshake");
            check_eq(isp.blocks(file), 4, "and takes the file from block 1: there is no resume on this path");
        }
        {
            FakeTftpBootloader d;
            d.mode = FakeTftpBootloader::Mode::Bootloader;
            FakeTftpBootloader::AckFault drop;
            drop.block = 2;
            drop.drop  = 11;
            d.ack_faults = { drop };
            Isp isp{ d };
            isp.send(hello());
            isp.send(data(1, file), 2);
            int silent = 0;
            for (int i = 0; i < 11; i++)
                silent += isp.send(data(2, file), 2).empty() ? 1 : 0;
            check_eq(silent, 11, "block 2 unACKed eleven times: ISP's flash fails (TFTPServer.cpp:1548-1605)");
            check_eq((long)isp.send(hello()).size(), 1, "with no power cut, a new handshake is answered");
            check_eq(isp.blocks(file), 4, "and the file taken again from block 1");
            check_eq(d.frames_not_isp(), 0, "a flash begun again after one that failed is ISP's (:1084)");
            check(isp.send(kFlashDone).size() == 1 && d.image() == file, "and it ends with the file");
        }
        {
            FakeTftpBootloader d;
            d.mode = FakeTftpBootloader::Mode::Bootloader;
            d.five_bytes_at_block = 2;
            Isp isp{ d };
            isp.send(hello());
            isp.send(data(1, file), 2);
            const auto r = isp.send(data(2, file), 2);
            check(r.size() == 1 && r[0].bytes.size() == 5, "five bytes in place of block 2's ACK (MySocket.cpp:193-196)");
        }
        {
            const Bytes small = file_of(700, 5);
            for (int kind = 0; kind < 3; kind++)
            {
                FakeTftpBootloader d;
                d.mode = FakeTftpBootloader::Mode::Bootloader;
                d.done = kind == 0 ? FakeTftpBootloader::Done::Silent : FakeTftpBootloader::Done::Wrong;
                d.early_done = kind == 2;
                Isp isp{ d };
                isp.send(hello());
                isp.send(data(1, small), 2);
                const auto last = isp.send(data(2, small), 2);
                const auto end = isp.send(kFlashDone);
                if (kind == 0)
                    check(end.empty() && !d.finished, "a device silent after its last ACK (TFTPServer.cpp:1415-1428)");
                else if (kind == 1)
                    check(end.size() == 1 && end[0].bytes == Bytes({ 0x00, 0x04, 0xFF, 0xFE }), "one that answers 00 04 FF FE");
                else
                    check(last.size() == 2 && last[1].bytes == kDone,
                          "one that sends 00 04 FF FF straight after the short block's ACK, which ISP ignores there");
            }
        }
    }
}

int run_fake_tftp_bootloader_tests()
{
    test_isp_flashes_a_running_device();
    test_isp_flashes_a_device_in_its_bootloader();
    test_datagrams_isp_would_not_send();
    test_faults_in_the_handshake();
    test_faults_on_blocks_and_the_end();
    return 0;
}
