#pragma once

// A Temco device that ISP flashes over the network, scripted, for the tests
// of T5000's TFTP flash (F4). The exchange is ISP's, from ISP\TFTPServer.cpp
// and ISP\MySocket.cpp:
//
//   TCP to the device's port, while its application runs:
//     64 bytes, EE 10 and 62 zeros            ->  40 bytes, 65 00, its IP at
//                                                  16, 18, 20 and 22; then it
//                                                  restarts in its bootloader
//     a Modbus TCP read of registers 0-99      ->  209 bytes: registers 7, 11,
//     on unit 255 (the read before a flash)        14 and 4 are the ones ISP uses
//   UDP to the device's port 10000, from ISP's 10001:
//     45 bytes, "Temcocontrols" ...            ->  31 bytes, "ReceiveDHCP", its
//                                                  IP, its bootloader's name
//     00 03 block data (512 bytes, the last    ->  00 04 block
//       maybe fewer), blocks from 1
//     "FLASH DONE"                             ->  00 04 FF FF
//
// ISP is the sender: there is no request for a file, no TFTP option and no
// change of port. The device's replies go to the port each datagram came
// from (reply_to_10001 sends them to 10001 instead: ISP's source cannot show
// which a real device does, as ISP sends from 10001 and listens there).
//
// It is an oracle as well as a device: every datagram and TCP message it
// hears is kept, and one ISP would not send at that point is kept with the
// reason; frames_not_isp() counts them. Timing is not judged.
//
// Choices where ISP's source does not settle what a real device does, each a
// field with its default:
//   - The 40-byte reply is zero but for 65 00 and the IP, the only bytes ISP
//     reads (MySocket.cpp:86-97).
//   - Bytes 26-30 of the 31-byte reply are zero: ISP never reads 27-30, and
//     reads 26 as a twelfth byte of the name (MySocket.cpp:200-212).
//   - The bootloader answers no EE 10, and answers the Modbus read only when
//     modbus_in_bootloader.
//   - It answers every "Temcocontrols", in its bootloader.
//   - A block sent again is taken again, in place: the image has each block
//     once, at (block - 1) * 512.
//
// Time is the caller's, as in fake_bootloader.h.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <algorithm>
#include <array>
#include <map>
#include <string>
#include <vector>

namespace t5000::testing
{
    using Bytes = std::vector<uint8_t>;

    inline constexpr uint16_t kFlashUdpPort = 10000;   // the device's (TFTPServer.h:20)
    inline constexpr uint16_t kLocalUdpPort = 10001;   // ISP's (TFTPServer.h:21)
    inline constexpr int kTftpBlockBytes    = 512;

    inline const char kTemcoHello[] = "Temcocontrols";   // 13 bytes, no NUL on the wire
    inline const char kTemcoReply[] = "ReceiveDHCP";     // 11
    inline const char kFlashDone[]  = "FLASH DONE";      // 10

    class FakeTftpBootloader
    {
    public:
        enum class Mode
        {
            Application,
            Bootloader,
        };

        struct Datagram
        {
            Bytes bytes;
            uint16_t port = 0;   // where it goes
            int delay_ms  = 0;
        };

        struct Heard
        {
            int64_t at_ms = 0;
            bool udp = true;
            Bytes bytes;
            std::string what;
            bool answered = false;
            std::string not_isp;
        };

        // A fault on one block's acknowledgement.
        struct AckFault
        {
            int block = 1;

            // This many sends of it go unanswered.
            int drop = 0;

            // Acknowledged with this block number instead; -1 for its own.
            int ack_as = -1;

            // Acknowledged this late.
            int delay_ms = 0;
        };

        // --- who it is -------------------------------------------------

        std::array<uint8_t, 4> ip = { 127, 0, 0, 1 };

        // Its bootloader's name, bytes 15-25 of its reply, at most 11.
        std::string name = "MINIPANEL";

        Mode mode = Mode::Application;

        // Answered to the Modbus read before a flash: 7 the product, 11 and
        // 14 the bootloader, 4 the firmware's low byte (TFTPServer.cpp:
        // 2078-2141).
        std::map<uint16_t, uint16_t> registers = { { 4, 26 }, { 7, 74 }, { 11, 62 }, { 14, 62 } };

        // --- choices -----------------------------------------------------

        bool modbus_in_bootloader = true;
        bool reply_to_10001       = false;

        // Silent this long after its 40-byte reply, while it restarts.
        int jump_ms = 0;

        // --- faults -----------------------------------------------------

        // This many "Temcocontrols" go unanswered first.
        int hello_silent = 0;

        // Answers only the first "Temcocontrols".
        bool answers_first_hello_only = false;

        // Its reply's length, IP and name, when not its own.
        int reply_length = 31;
        std::array<uint8_t, 4> reply_ip = { 0, 0, 0, 0 };
        bool reply_ip_set = false;

        // The 40-byte reply's length, when not 40; and no reply at all.
        int runtime_reply_length = 40;
        bool runtime_silent      = false;

        std::vector<AckFault> ack_faults;

        // Block 1's ACK comes this late: the flash's erase.
        int first_block_ms = 0;

        // After this many blocks it answers nothing until power_on().
        int cut_off_after_blocks = -1;

        // In reply to this block, 5 bytes instead of its ACK: ISP only
        // prints a failure for them (MySocket.cpp:193-196). -1 for never.
        int five_bytes_at_block = -1;

        enum class Done
        {
            Answer,    // 00 04 FF FF to each FLASH DONE
            Silent,
            Wrong,     // 00 04 FF FE
        };
        Done done = Done::Answer;

        // 00 04 FF FF also straight after a short block's ACK, before ISP
        // sends FLASH DONE: ISP takes it only in reply to that
        // (MySocket.cpp:302-313).
        bool early_done = false;

        // --- what happened ----------------------------------------------

        std::vector<Heard> heard;
        std::map<int, Bytes> blocks;   // by block number
        std::map<int, int> sends;      // how many times each block came
        int hellos      = 0;
        int flash_dones = 0;
        bool finished   = false;

        void power_on(int64_t now_ms)
        {
            m_dead = false;
            mode   = Mode::Bootloader;
            m_quiet_until = now_ms;
            m_greeted = false;
            m_last_block = 0;
            m_short_seen = false;
            m_done_seen  = false;
        }

        // Bytes on the TCP connection: one whole message.
        Bytes tcp(const Bytes& b, int64_t now_ms)
        {
            Heard h;
            h.at_ms = now_ms;
            h.udp   = false;
            h.bytes = b;

            Bytes out;
            if (b.size() >= 2 && b[0] == 0xEE && b[1] == 0x10)
            {
                h.what = "EE 10";
                bool zeros = true;
                for (size_t i = 2; i < b.size(); i++)
                    zeros = zeros && b[i] == 0;
                if (b.size() != 64 || !zeros)
                    h.not_isp = "an EE 10 that is not 64 bytes, 62 of them zero (TFTPServer.cpp:1093-1095)";
                if (mode == Mode::Application && !m_dead && !runtime_silent)
                {
                    out.assign((size_t)runtime_reply_length, 0);
                    if (out.size() >= 23)
                    {
                        out[0]  = 0x65;
                        out[1]  = 0x00;
                        out[16] = ip[0];
                        out[18] = ip[1];
                        out[20] = ip[2];
                        out[22] = ip[3];
                    }
                    mode = Mode::Bootloader;
                    m_quiet_until = now_ms + jump_ms;
                }
            }
            else if (b.size() >= 8 && b[2] == 0 && b[3] == 0 && b[7] == 0x03)
            {
                h.what = "Modbus read";
                const bool isp = b.size() == 12 && b[4] == 0 && b[5] == 6 && b[6] == 255 && b[8] == 0 && b[9] == 0 &&
                                 b[10] == 0 && b[11] == 100;
                if (!isp)
                    h.not_isp = "ISP reads only registers 0-99 of unit 255 (TFTPServer.cpp:2092)";
                const bool answers = !m_dead && (mode == Mode::Application || modbus_in_bootloader) && now_ms >= m_quiet_until;
                if (answers && b.size() == 12)
                {
                    const uint16_t first = (uint16_t)(b[8] << 8 | b[9]);
                    const uint16_t count = (uint16_t)(b[10] << 8 | b[11]);
                    if (count >= 1 && count <= 125)
                    {
                        const size_t n = 3 + (size_t)count * 2;
                        out = { b[0], b[1], 0, 0, (uint8_t)(n >> 8), (uint8_t)(n & 0xFF), b[6], 0x03, (uint8_t)(count * 2) };
                        for (uint16_t i = 0; i < count; i++)
                        {
                            auto r = registers.find((uint16_t)(first + i));
                            const uint16_t v = r == registers.end() ? 0 : r->second;
                            out.push_back((uint8_t)(v >> 8));
                            out.push_back((uint8_t)(v & 0xFF));
                        }
                    }
                }
            }
            else
            {
                h.what    = "TCP of " + std::to_string(b.size()) + " bytes";
                h.not_isp = "neither EE 10 nor the Modbus read";
            }
            h.answered = !out.empty();
            heard.push_back(h);
            return out;
        }

        // A datagram to its port 10000, from the given port.
        std::vector<Datagram> udp(const Bytes& b, uint16_t from_port, int64_t now_ms)
        {
            Heard h;
            h.at_ms = now_ms;
            h.bytes = b;
            std::vector<Datagram> out;
            const uint16_t to = reply_to_10001 ? kLocalUdpPort : from_port;
            const bool listening = !m_dead && mode == Mode::Bootloader && now_ms >= m_quiet_until;

            if (starts_with(b, kTemcoHello))
            {
                h.what = "Temcocontrols";
                hellos++;
                h.not_isp = hello_not_isp(b);
                if (m_done_seen)
                {
                    // A new flash.
                    m_last_block = 0;
                    m_short_seen = false;
                    m_done_seen  = false;
                }
                const int n = m_hellos_heard++;
                const bool answer = listening && n >= hello_silent && !(answers_first_hello_only && m_greeted);
                if (answer)
                {
                    m_greeted = true;
                    out.push_back({ hello_reply(), to, 0 });
                }
            }
            else if (b.size() >= 4 && b[0] == 0x00 && b[1] == 0x03)
            {
                const int k = b[2] << 8 | b[3];
                h.what = "DATA " + std::to_string(k);
                h.not_isp = data_not_isp(b, k);
                if (listening)
                    out = take_block(b, k, to);
            }
            else if (b == Bytes(kFlashDone, kFlashDone + 10))
            {
                h.what = "FLASH DONE";
                flash_dones++;
                if (m_last_block == 0)
                    h.not_isp = "FLASH DONE before any block";
                m_done_seen = true;
                if (listening)
                {
                    if (done == Done::Answer)
                        out.push_back({ { 0x00, 0x04, 0xFF, 0xFF }, to, 0 });
                    else if (done == Done::Wrong)
                        out.push_back({ { 0x00, 0x04, 0xFF, 0xFE }, to, 0 });
                    finished = finished || done == Done::Answer;
                }
            }
            else
            {
                h.what    = "UDP of " + std::to_string(b.size()) + " bytes";
                h.not_isp = "not a datagram ISP sends";
            }
            h.answered = !out.empty();
            heard.push_back(h);
            return out;
        }

        // The file as it arrived: block k at (k - 1) * 512.
        Bytes image() const
        {
            Bytes out;
            for (const auto& [k, data] : blocks)
            {
                const size_t at = (size_t)(k - 1) * kTftpBlockBytes;
                if (out.size() < at + data.size())
                    out.resize(at + data.size(), 0xFF);
                std::copy(data.begin(), data.end(), out.begin() + (ptrdiff_t)at);
            }
            return out;
        }

        int frames_not_isp() const
        {
            int n = 0;
            for (const Heard& x : heard)
                if (!x.not_isp.empty())
                    n++;
            return n;
        }

        std::vector<std::string> not_isp_reasons() const
        {
            std::vector<std::string> out;
            for (const Heard& x : heard)
                if (!x.not_isp.empty())
                    out.push_back(x.what + ": " + x.not_isp);
            return out;
        }

        bool cut_off() const { return m_dead; }

    private:
        bool m_dead = false;
        int64_t m_quiet_until = 0;
        bool m_greeted    = false;
        int m_hellos_heard = 0;
        int m_last_block  = 0;
        bool m_short_seen = false;
        bool m_done_seen  = false;

        static bool starts_with(const Bytes& b, const char* s)
        {
            const size_t n = strlen(s);
            return b.size() >= n && std::equal(s, s + n, b.begin(), [](char c, uint8_t x) { return (uint8_t)c == x; });
        }

        // ISP's "Temcocontrols" (TFTPServer.cpp:511-562): the assigned IP at
        // 13, FF FF FF 00 at 17, zeros at 21, the device's IP at 25, zeros to
        // the end.
        static std::string hello_not_isp(const Bytes& b)
        {
            if (b.size() != 45)
                return "a Temcocontrols of " + std::to_string(b.size()) + " bytes, not 45";
            if (b[17] != 0xFF || b[18] != 0xFF || b[19] != 0xFF || b[20] != 0x00)
                return "a mask other than FF FF FF 00";
            for (size_t i = 21; i < 25; i++)
                if (b[i] != 0)
                    return "bytes 21-24 not zero";
            for (size_t i = 29; i < 45; i++)
                if (b[i] != 0)
                    return "bytes 29-44 not zero";
            return "";
        }

        Bytes hello_reply() const
        {
            Bytes r((size_t)reply_length, 0);
            const std::array<uint8_t, 4>& from = reply_ip_set ? reply_ip : ip;
            for (size_t i = 0; i < 11 && i < r.size(); i++)
                r[i] = (uint8_t)kTemcoReply[i];
            for (size_t i = 0; i < 4 && 11 + i < r.size(); i++)
                r[11 + i] = from[i];
            for (size_t i = 0; i < name.size() && i < 11 && 15 + i < r.size(); i++)
                r[15 + i] = (uint8_t)name[i];
            return r;
        }

        std::string data_not_isp(const Bytes& b, int k) const
        {
            if (b.size() > 4 + (size_t)kTftpBlockBytes)
                return "a block of more than 512 bytes";
            if (!m_greeted)
                return "a block before the bootloader's reply";
            if (m_done_seen)
                return "a block after FLASH DONE";
            if (m_last_block == 0)
                return k == 1 ? "" : "a first block numbered " + std::to_string(k);
            if (k == m_last_block)
                return "";   // sent again
            if (k != m_last_block + 1)
                return "block " + std::to_string(k) + " after block " + std::to_string(m_last_block);
            if (m_short_seen)
                return "a block after a short one";
            return "";
        }

        std::vector<Datagram> take_block(const Bytes& b, int k, uint16_t to)
        {
            std::vector<Datagram> out;
            if (cut_off_after_blocks >= 0 && k > cut_off_after_blocks)
            {
                m_dead = true;
                return out;
            }

            const int n = sends[k]++;
            blocks[k].assign(b.begin() + 4, b.end());
            const bool short_block = blocks[k].size() < (size_t)kTftpBlockBytes;
            m_short_seen = m_short_seen || short_block;
            if (k >= m_last_block)
                m_last_block = k;

            if (k == five_bytes_at_block)
            {
                out.push_back({ { 0x05, 0x00, 0x00, 0x00, 0x00 }, to, 0 });
                return out;
            }

            int ack = k;
            int delay = k == 1 ? first_block_ms : 0;
            for (const AckFault& f : ack_faults)
            {
                if (f.block != k)
                    continue;
                if (n < f.drop)
                    return out;
                if (f.ack_as >= 0)
                    ack = f.ack_as;
                delay += f.delay_ms;
            }
            out.push_back({ { 0x00, 0x04, (uint8_t)(ack >> 8), (uint8_t)(ack & 0xFF) }, to, delay });
            if (early_done && short_block)
                out.push_back({ { 0x00, 0x04, 0xFF, 0xFF }, to, delay });
            return out;
        }
    };
}
