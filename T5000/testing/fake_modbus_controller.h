#pragma once

// A controller with synthetic bootloaders on its RS485 bus, as ISP flashes a
// device through one: Modbus TCP to the controller, the device named by the
// unit byte (ComWriter.cpp:2220-2267). For the tests of T5000's flash behind
// a controller (F5).
//
// The frames are ISP's, from ModbusDllforVc\ModbusDllforVc\common.cpp: an
// MBAP header, then the device's frame without its CRC.
//
//   tt tt 00 00 00 06 unit fc ...
//
// The length field is 00 06 on every request, the 141-byte block too
// (common.cpp:1847-1848, 2891-2892, 4329-4330, 9306-9307), so a request is
// cut by its function code, never by that field: 12 bytes for a read or a
// write, 13 and the byte count for a block.
//
// Unit 255 is the controller itself: registers 0-99 (4 and 5 its firmware, 7
// its model), and register 99, by which ISP asks it to quiet its MS/TP bus
// before flashing a device on it (ComWriter.cpp:2278-2332): a write of 1,
// then reads until it says 2, done, or 3, could not. Every other unit is
// relayed to the device with that id, and the device's answer comes back
// with the request's transaction id.
//
// A reply is one piece, as ISP reads it with one recv (common.cpp:1900,
// 2993, 4357, 9341); a fault can split it, send it late, or reset or close
// the connection. After a reset the controller hears nothing until the
// client connects again, and the devices stay as they were. What comes next
// is ISP's to choose: on a reset its write reconnects and returns -1
// (common.cpp:4360-4366). flash_a_tstat_RAM, an ARM .hex, retries it,
// sending the block again on the new connection (ComWriter.cpp:1116-1132);
// flash_a_tstat, a data .hex, takes it as done and goes on (:1003-1021).
//
// The controller judges what is sent to it, as the devices do, and
// frames_not_isp() counts both. What a real controller does with a device's
// reply that arrives garbled on its bus is not known: this one drops it.

#include <stdint.h>

#include <algorithm>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "fake_bootloader.h"

namespace t5000::testing
{
    // Register 99's states (ComWriter.cpp:2269-2275).
    inline constexpr uint16_t kRegShutdown     = 99;
    inline constexpr uint16_t kShutdownInitial = 0;
    inline constexpr uint16_t kShutdownStart   = 1;
    inline constexpr uint16_t kShutdownSuccess = 2;
    inline constexpr uint16_t kShutdownTimeout = 3;

    inline constexpr uint8_t kControllerUnit = 255;

    // The controllers ISP asks to quiet their bus, by register 7: a
    // MiniPanel, a TStat10, a MiniPanel ARM and an ESP32 T3; and the firmware
    // they must have, register 5 * 10 + register 4 (ComWriter.cpp:2423-2431).
    inline constexpr uint16_t kQuietedModels[] = { 35, 10, 74, 88 };
    inline constexpr int kQuietFromFirmware    = 636;

    class FakeModbusController
    {
    public:
        // A fault on the connection, matched against each request.
        struct Fault
        {
            enum class Action
            {
                Silence,   // no reply
                Late,      // the reply comes delay_ms late
                Split,     // the reply comes in two pieces
                Reset,     // the connection is reset after the request; no reply
                Close,     // the reply, then the connection is closed
            };

            int unit = -1;   // -1 for any
            int fc   = -1;
            int reg  = -1;
            Action action = Action::Silence;
            int delay_ms  = 0;
            int skip  = 0;
            int times = 1;
            int matched = 0;
        };

        struct Reply
        {
            bool silent = true;
            Bytes bytes;
            int delay_ms = 0;

            // Sent as two pieces, the first `split_at` bytes and the rest.
            size_t split_at = 0;

            enum class After
            {
                Nothing,
                Reset,
                Close,
            };
            After after = After::Nothing;
        };

        struct Heard
        {
            int64_t at_ms = 0;
            Bytes request;
            std::string what;
            bool answered = false;
            std::string not_isp;
        };

        // Unit 255's registers, 0-99; anything not here reads 0.
        std::map<uint16_t, uint16_t> registers = { { 4, 6 }, { 5, 63 }, { 7, 74 } };

        std::vector<FakeBootloader> devices;

        // Register 99: after the write of 1, this many reads say 1, still
        // working; then shutdown_result. A write of 1 goes unanswered when
        // echoes_shutdown is false, and is not acted on.
        int shutdown_polls_busy   = 0;
        uint16_t shutdown_result  = kShutdownSuccess;
        bool echoes_shutdown      = true;

        // The bus: added to every device's answer.
        int relay_ms = 0;

        std::vector<Fault> faults;

        std::vector<Heard> heard;
        std::vector<uint16_t> transaction_ids;
        bool connected  = true;
        int connections = 1;

        FakeBootloader* device(uint8_t id)
        {
            for (FakeBootloader& d : devices)
                if (d.id == id)
                    return &d;
            return nullptr;
        }

        // A new connection from the client.
        void connect()
        {
            connected = true;
            connections++;
        }

        // How long a request is, from its first bytes: 0 when there are not
        // yet enough to tell, -1 when it is not one ISP sends.
        static int request_length(const Bytes& b)
        {
            if (b.size() < 8)
                return 0;
            if (b[7] == 0x03 || b[7] == 0x06)
                return 12;
            if (b[7] != 0x10)
                return -1;
            if (b.size() < 13)
                return 0;
            return 13 + b[12];
        }

        // One whole request, MBAP header and all.
        Reply request(const Bytes& adu, int64_t now_ms)
        {
            Reply out;
            Heard h;
            h.at_ms   = now_ms;
            h.request = adu;

            if (!connected)
            {
                h.what = "a request with no connection";
                heard.push_back(h);
                return out;
            }

            const int length = request_length(adu);
            if (length <= 0 || (size_t)length != adu.size())
            {
                h.what    = "a request of " + std::to_string(adu.size()) + " bytes";
                h.not_isp = "not a read, write or block as ISP frames them";
                heard.push_back(h);
                return out;
            }

            const uint16_t tid = (uint16_t)(adu[0] << 8 | adu[1]);
            transaction_ids.push_back(tid);
            const uint8_t unit = adu[6];
            const uint8_t fc   = adu[7];
            const uint16_t reg = (uint16_t)(adu[8] << 8 | adu[9]);
            h.what = "unit " + std::to_string(unit) + " fc " + std::to_string(fc) + " at " + std::to_string(reg);

            if (adu[2] != 0 || adu[3] != 0)
                h.not_isp = "a protocol id other than 00 00";
            else if (adu[4] != 0 || adu[5] != 6)
                h.not_isp = "a length field other than 00 06, which ISP sends on every request";

            Fault* fault = match_fault(unit, fc, reg);
            if (fault && fault->action == Fault::Action::Reset)
            {
                connected = false;
                h.what += " (reset)";
                heard.push_back(h);
                out.after = Reply::After::Reset;
                return out;
            }

            const Bytes pdu(adu.begin() + 6, adu.end());
            ModbusAnswer a;
            if (unit == kControllerUnit)
                a = own(pdu, h.not_isp);
            else if (FakeBootloader* d = device(unit))
            {
                a = d->hear(pdu, now_ms);
                a.delay_ms += relay_ms;
                if (a.garble)
                    a.silent = true;
            }

            if (fault && fault->action == Fault::Action::Silence)
                a.silent = true;
            h.answered = !a.silent;
            heard.push_back(h);
            if (a.silent)
                return out;

            out.silent   = false;
            out.delay_ms = a.delay_ms;
            out.bytes    = { adu[0], adu[1], 0, 0, (uint8_t)(a.reply.size() >> 8), (uint8_t)(a.reply.size() & 0xFF) };
            out.bytes.insert(out.bytes.end(), a.reply.begin(), a.reply.end());
            if (fault)
            {
                switch (fault->action)
                {
                case Fault::Action::Late:
                    out.delay_ms += fault->delay_ms;
                    break;
                case Fault::Action::Split:
                    out.split_at = out.bytes.size() / 2;
                    break;
                case Fault::Action::Close:
                    out.after = Reply::After::Close;
                    connected = false;
                    break;
                default:
                    break;
                }
            }
            return out;
        }

        int frames_not_isp() const
        {
            int n = 0;
            for (const Heard& x : heard)
                if (!x.not_isp.empty())
                    n++;
            for (const FakeBootloader& d : devices)
                n += d.frames_not_isp();
            return n;
        }

        std::vector<std::string> not_isp_reasons() const
        {
            std::vector<std::string> out;
            for (const Heard& x : heard)
                if (!x.not_isp.empty())
                    out.push_back(x.what + ": " + x.not_isp);
            for (const FakeBootloader& d : devices)
                for (const std::string& r : d.not_isp_reasons())
                    out.push_back("unit " + std::to_string(d.id) + " " + r);
            return out;
        }

        uint16_t shutdown_state() const { return m_shutdown; }

    private:
        uint16_t m_shutdown = kShutdownInitial;
        int m_busy_polls    = 0;

        Fault* match_fault(uint8_t unit, uint8_t fc, uint16_t reg)
        {
            for (Fault& x : faults)
            {
                if ((x.unit >= 0 && x.unit != unit) || (x.fc >= 0 && x.fc != fc) || (x.reg >= 0 && x.reg != reg))
                    continue;
                const int n = x.matched++;
                if (n < x.skip)
                    continue;
                if (x.times >= 0 && n >= x.skip + x.times)
                    continue;
                return &x;
            }
            return nullptr;
        }

        uint16_t read_own(uint16_t reg)
        {
            if (reg == kRegShutdown)
            {
                if (m_shutdown == kShutdownStart)
                {
                    if (m_busy_polls < shutdown_polls_busy)
                    {
                        m_busy_polls++;
                        return kShutdownStart;
                    }
                    m_shutdown = shutdown_result;
                }
                return m_shutdown;
            }
            auto r = registers.find(reg);
            return r == registers.end() ? 0 : r->second;
        }

        int firmware() { return read_own(5) * 10 + read_own(4); }

        bool asked_to_quiet()
        {
            const uint16_t model = read_own(7);
            return std::find(std::begin(kQuietedModels), std::end(kQuietedModels), model) != std::end(kQuietedModels) &&
                   firmware() >= kQuietFromFirmware;
        }

        // Unit 255: the reads ISP makes of the controller are 0-99 (:2419),
        // 99 (:2298) and 1, the wake-up before a block's last tries (:1013);
        // its one write is 99 = 1 (:2287).
        ModbusAnswer own(const Bytes& pdu, std::string& not_isp)
        {
            ModbusAnswer a;
            const uint8_t fc   = pdu[1];
            const uint16_t reg = (uint16_t)(pdu[2] << 8 | pdu[3]);
            const uint16_t val = (uint16_t)(pdu[4] << 8 | pdu[5]);
            if (fc == 0x03)
            {
                const bool isp = (reg == 0 && val == 100) || (reg == kRegShutdown && val == 1) || (reg == 1 && val == 1);
                if (!isp && not_isp.empty())
                    not_isp = "ISP never reads " + std::to_string(val) + " at " + std::to_string(reg) + " of the controller";
                if (val == 0 || val > 125)
                {
                    a.silent = false;
                    a.reply  = { pdu[0], 0x83, 3 };
                    return a;
                }
                a.silent = false;
                a.reply  = { pdu[0], 0x03, (uint8_t)(val * 2) };
                for (uint16_t i = 0; i < val; i++)
                {
                    const uint16_t v = read_own((uint16_t)(reg + i));
                    a.reply.push_back((uint8_t)(v >> 8));
                    a.reply.push_back((uint8_t)(v & 0xFF));
                }
                return a;
            }
            if (fc == 0x06)
            {
                if (!(reg == kRegShutdown && val == kShutdownStart) && not_isp.empty())
                    not_isp = "ISP writes only 99 = 1 to the controller";
                else if (!asked_to_quiet() && not_isp.empty())
                    not_isp = "99 = 1 to a controller ISP does not ask to quiet its bus: model " +
                              std::to_string(read_own(7)) + ", firmware " + std::to_string(firmware());
                if (reg == kRegShutdown && !echoes_shutdown)
                    return a;
                if (reg == kRegShutdown && val == kShutdownStart)
                {
                    m_shutdown   = kShutdownStart;
                    m_busy_polls = 0;
                }
                else
                    registers[reg] = val;
                a.silent = false;
                a.reply  = pdu;
                return a;
            }
            if (not_isp.empty())
                not_isp = "ISP sends the controller only reads and one write";
            return a;
        }
    };
}
