#pragma once

// A Temco device that ISP can flash over Modbus, scripted, for the tests of
// T5000's firmware transfer: on a serial line (fake_rtu_bus.h) and behind a
// controller (fake_modbus_controller.h). This is the device alone, with no
// framing: it hears "unit fc data" and answers the same, without a CRC or an
// MBAP header, and the bus or the controller adds them.
//
// It runs as an application until 127 is written to register 16, and then as
// its bootloader, which takes ISP's commands on register 16 (0x7F, 0x3F, 0x1F,
// 1 and 8), its bank on register 12 or 33, the file's MD5 and size in
// registers 1993-1998, and the file itself in 128-byte blocks. The frames are
// ISP's, from ISP\ComWriter.cpp and ModbusDllforVc\ModbusDllforVc\common.cpp:
//
//   read      id 03 hi lo 00 nn          reply id 03 2n data
//   write     id 06 hi lo vh vl          reply the same
//   block     id 10 ah al 00 80 80 data  reply id 10 ah al 00 80
//
// A block's register count is its byte count, 0x0080, not the 0x0040 of
// standard Modbus, and its address is the block's byte offset in its bank,
// sixteen bits wide (common.cpp:4225-4241, 4305-4312).
//
// It is an oracle as well as a device. Every frame it hears is kept, and one
// that ISP would not send at that point of a flash is kept with the reason:
// frames_not_isp() is the count, as frames_not_scan_frames() is for the
// serial scan, so a test of T5000's flash can say it sent nothing ISP would
// not. A few of ISP's own slips are counted too, such as an ESP32 flash that
// erases and then starts past block 0 (ComWriter.cpp:976-981): T5000 is not
// meant to copy them. Timing is not judged: ISP's reply windows depend on
// the platform's timer.
//
// Where ISP's source does not settle what a real device does, this one does
// something named and stated. Each such choice is a field with its default,
// not a fact about Temco's bootloaders:
//
//   - The bootloader echoes 0x7F and 0x3F (echoes_init_and_erase). ISP also
//     takes silence, which an ARM bootloader is said to give (ComWriter.cpp:
//     2754).
//   - Register 0xEE10, the update's state, reads 0 until 0x1F is written,
//     then 0x1F until the flash ends with 16=1. A flash cut short therefore
//     reads 0x1F, which ISP takes as interrupted (:861, 2636).
//   - 16=1 is echoed and the device stays in its bootloader, before the
//     blocks (ISP's "reset", :582-606, 1578-1594) and after them. ISP's
//     second thread sends 16=8 and 33=1 after the end, and may then flash a
//     second section (:1737-1766), so staying is what takes all of ISP's
//     flows. end_restarts makes 16=1 after the blocks restart the
//     application instead.
//   - Register 33 selects the bank, as register 12 does. 16=8 is echoed and
//     does nothing.
//   - Register 1991 counts the blocks taken since the last erase, a block
//     written twice counting once (packets_override sets it).
//   - Registers 1992-1999 are plain storage, and they and 0xEE10 are the
//     same in the application and the bootloader, so both of ISP's maps of
//     them work (:852-854, 868, 932-937 and, before the jump, :1442-1476).
//   - An erase clears the bank it is in.
//   - It does not answer id 255 (answers_id_255), and does nothing a frame to
//     255 asks.
//   - A block after a resume is kept where it was sent. ISP's ARM thread
//     sends a resumed section's blocks from address 0 (:2910, 1096-1121);
//     whether the device adds its own offset is not known. flash_a_tstat
//     sends an ESP32's from 1991 * 128 (:885, 976-981). Either is taken for
//     the session's first block only; a later section starts at 0.
//   - A first block past 0 after an erase is judged, though ISP can send
//     one. An ESP32 found in its bootloader with a matching MD5 and 1994
//     past 0 (:1434-1463), whose 0xEE10 then reads neither 0x40 nor 0x1F, is
//     erased (:919-958) and sent from 1994 * 128 (:976-981), its start left
//     unwritten. T5000 should not do that.
//
// Time is the caller's: every frame comes with now_ms. A test passes a clock
// of its own and never waits; a host passes the real one.

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace t5000::testing
{
    using Bytes = std::vector<uint8_t>;

    // Registers ISP uses.
    inline constexpr uint16_t kRegDeviceId  = 6;
    inline constexpr uint16_t kRegProduct   = 7;
    inline constexpr uint16_t kRegBoot11    = 11;
    inline constexpr uint16_t kRegSection   = 12;
    inline constexpr uint16_t kRegBoot14    = 14;
    inline constexpr uint16_t kRegCommand   = 16;
    inline constexpr uint16_t kRegEspStatus = 23;
    inline constexpr uint16_t kRegBank      = 33;
    inline constexpr uint16_t kRegPackets   = 1991;
    inline constexpr uint16_t kRegMd5       = 1993;
    inline constexpr uint16_t kRegSizeHigh  = 1997;
    inline constexpr uint16_t kRegSizeLow   = 1998;
    inline constexpr uint16_t kRegStatus    = 0xEE10;
    inline constexpr uint16_t kRegCpu       = 65010;

    // What ISP writes to register 16. The names are from ISP's messages;
    // only the numbers are certain.
    inline constexpr uint16_t kCmdJump   = 127;      // 0x7F: jump; in the bootloader, init
    inline constexpr uint16_t kCmdErase  = 0x3F;
    inline constexpr uint16_t kCmdStart  = 0x1F;
    inline constexpr uint16_t kCmdEnd    = 1;
    inline constexpr uint16_t kCmdAfter0 = 8;        // after section 0, in ISP's second thread
    inline constexpr uint16_t kCmdQuiet  = 0x0455;   // to 255: MS/TP devices keep quiet

    inline constexpr int kBlockBytes = 128;

    // The update-state values ISP takes as interrupted (ComWriter.cpp:861).
    inline constexpr uint16_t kStatusInterrupted  = 0x1F;
    inline constexpr uint16_t kStatusInterrupted2 = 0x40;

    // A device's answer to one frame.
    struct ModbusAnswer
    {
        bool silent = true;

        // "unit fc data": no CRC, no MBAP header.
        Bytes reply;

        // How long after the request it comes.
        int delay_ms = 0;

        // The line damages it: a bad CRC on a serial line. Behind a
        // controller it is dropped on the bus, and never reaches the client;
        // WrongId is the wrong unit there.
        bool garble = false;
    };

    // A fault, matched against each frame the device hears. The first in the
    // list that matches a frame applies to it; the others do not count it.
    struct ModbusFault
    {
        enum class Kind
        {
            Any,
            Read,
            Write,
            Block,
        };

        enum class Action
        {
            Deaf,         // the request is lost: not heard, not acted on
            Silence,      // heard and acted on; the reply is lost
            Garble,       // the reply arrives damaged
            WrongId,      // the reply names another id
            WrongValue,   // a write echoes another value, a read gives two bytes
                          // too few, a block's reply has the standard count 00 40
            Exception,    // a Modbus exception: fc | 0x80, code
            Late,         // the reply comes delay_ms late
        };

        Kind kind = Kind::Any;

        // A read's or write's first register; -1 for any.
        int reg = -1;

        // A write's value; -1 for any.
        int value = -1;

        // A block's bank and address; -1 for any.
        int bank    = -1;
        int address = -1;

        Action action = Action::Silence;
        uint8_t code  = 2;
        int delay_ms  = 0;

        // Lets this many matching frames through, then applies to this many;
        // -1 for every one after.
        int skip  = 0;
        int times = 1;

        int matched = 0;
    };

    class FakeBootloader
    {
    public:
        enum class Mode
        {
            Application,
            Bootloader,
        };

        // A frame the device heard.
        struct Heard
        {
            int64_t at_ms = 0;
            Bytes frame;
            Mode mode = Mode::Application;
            std::string what;
            bool answered = false;

            // Empty when ISP sends such a frame at this point of a flash.
            std::string not_isp;
        };

        // A block the bootloader took, in the order it came.
        struct Block
        {
            int seq  = 0;
            int bank = 0;
            uint16_t address = 0;
            Bytes data;
        };

        // --- who it is -------------------------------------------------

        uint8_t id = 1;
        Mode mode  = Mode::Application;

        // What the application and the bootloader answer with; anything not
        // here reads 0. set_identity fills them; a test may change them.
        std::map<uint16_t, uint16_t> app_registers;
        std::map<uint16_t, uint16_t> boot_registers;

        // The update's registers 1992-1999, the same in both.
        std::map<uint16_t, uint16_t> update_registers;

        // The rate it answers at; 0 for every rate. A TStat10 with an old
        // bootloader runs its application at 76800 and its bootloader at
        // 57600 (ComWriter.cpp:1959-1981, 2530-2538).
        int app_baud  = 0;
        int boot_baud = 0;

        // --- choices, see the top of this file --------------------------

        bool echoes_init_and_erase = true;
        bool end_restarts          = false;
        bool answers_id_255        = false;
        int packets_override       = -1;

        // Silent this long: after the jump, after the erase's echo, and
        // after an end that restarts.
        int jump_ms    = 0;
        int erase_ms   = 0;
        int restart_ms = 0;

        // The update's state, register 0xEE10. A test sets 0x1F or 0x40 for
        // a device whose last flash was cut short.
        uint16_t status = 0;

        // --- faults -----------------------------------------------------

        std::vector<ModbusFault> faults;

        // After this many blocks since the last erase it answers nothing, as
        // if its power were cut, until power_on(): the next block is not
        // taken. -1 for never; set it back for a flash after the cut.
        int cut_off_after_blocks = -1;

        // --- what happened ----------------------------------------------

        std::vector<Heard> heard;
        std::vector<Block> blocks;
        int erases = 0;
        int jumps  = 0;

        FakeBootloader() { set_identity(1, 12345678, 74, 538, 62); }

        // Registers 0-3 the serial number a byte each, 4 and 5 the firmware,
        // 6 the id, 7 the product, 8 the hardware. The application keeps
        // its bootloader's version in register 14 with 11 at 1; the
        // bootloader reports it in 11 with 14 at 0, the two states ISP tells
        // apart for a TStat10 (ComWriter.cpp:1966-1974). ISP takes the
        // larger of the two (:2027).
        void set_identity(uint8_t new_id, uint32_t serial, uint16_t product, uint16_t firmware, uint16_t bootloader)
        {
            id = new_id;
            app_registers.clear();
            boot_registers.clear();
            for (auto* r : { &app_registers, &boot_registers })
            {
                (*r)[0] = (uint16_t)(serial & 0xFF);
                (*r)[1] = (uint16_t)((serial >> 8) & 0xFF);
                (*r)[2] = (uint16_t)((serial >> 16) & 0xFF);
                (*r)[3] = (uint16_t)((serial >> 24) & 0xFF);
                (*r)[4] = (uint16_t)(firmware & 0xFF);
                (*r)[5] = (uint16_t)(firmware >> 8);
                (*r)[kRegDeviceId] = new_id;
                (*r)[kRegProduct]  = product;
                (*r)[8] = 26;
            }
            app_registers[kRegBoot11]  = 1;
            app_registers[kRegBoot14]  = bootloader;
            boot_registers[kRegBoot11] = bootloader;
            boot_registers[kRegBoot14] = 0;
        }

        // The rate it answers at now; 0 for any.
        int baud() const { return mode == Mode::Application ? app_baud : boot_baud; }

        // Whether a frame to this unit is for this device to hear.
        bool addressed(uint8_t unit) const { return unit == id || unit == 255; }

        // Back after a cut: in its bootloader, as a device whose flash was
        // cut short would be, with its blocks and registers kept.
        void power_on(int64_t now_ms)
        {
            m_dead = false;
            mode   = Mode::Bootloader;
            m_quiet_until = now_ms;
            begin_session();
        }

        bool cut_off() const { return m_dead; }

        // A frame on the line or through the controller, "unit fc data".
        ModbusAnswer hear(const Bytes& frame, int64_t now_ms)
        {
            ModbusAnswer answer;
            if (frame.size() < 2 || !addressed(frame[0]))
                return answer;

            Heard h;
            h.at_ms = now_ms;
            h.frame = frame;
            h.mode  = mode;
            h.what  = describe(frame);

            if (mode == Mode::Bootloader && !m_session)
                begin_session();

            if (!m_dead && mode == Mode::Bootloader && frame[1] == 0x10 && cut_off_after_blocks >= 0 &&
                (int)m_packets.size() >= cut_off_after_blocks)
                m_dead = true;

            if (m_dead || now_ms < m_quiet_until)
            {
                h.what += m_dead ? " (cut off)" : " (busy)";
                heard.push_back(h);
                return answer;
            }

            ModbusFault* fault = match_fault(frame);
            if (fault && fault->action == ModbusFault::Action::Deaf)
            {
                h.what += " (lost)";
                heard.push_back(h);
                return answer;
            }

            const uint8_t unit = frame[0];
            const uint8_t fc   = frame[1];
            const bool acts    = unit == id || answers_id_255;
            bool reply = acts;
            switch (fc)
            {
            case 0x03:
                answer.reply = read(frame, h.not_isp);
                break;
            case 0x06:
                answer.reply = write(frame, now_ms, acts, h.not_isp, reply);
                break;
            case 0x10:
                answer.reply = acts ? block(frame, h.not_isp, reply) : Bytes();
                break;
            default:
                h.not_isp = "function code " + std::to_string(fc) + ", which ISP never sends";
                answer.reply = { unit, (uint8_t)(fc | 0x80), 1 };
                break;
            }

            if (answer.reply.empty())
                reply = false;
            if (fault && fault->action == ModbusFault::Action::Silence)
                reply = false;
            else if (reply && fault)
                apply(*fault, frame, answer);

            answer.silent = !reply;
            if (!reply)
                answer.reply.clear();
            h.answered = reply;
            heard.push_back(h);
            return answer;
        }

        // Every frame heard that ISP would not send there.
        int frames_not_isp() const
        {
            int n = 0;
            for (const Heard& h : heard)
                if (!h.not_isp.empty())
                    n++;
            return n;
        }

        std::vector<std::string> not_isp_reasons() const
        {
            std::vector<std::string> out;
            for (const Heard& h : heard)
                if (!h.not_isp.empty())
                    out.push_back(h.what + ": " + h.not_isp);
            return out;
        }

        // How many writes of this register, of this value or any, it heard.
        int writes_of(uint16_t reg, int value = -1) const
        {
            int n = 0;
            for (const Heard& h : heard)
                if (h.frame.size() >= 6 && h.frame[1] == 0x06 && reg_of(h.frame) == reg &&
                    (value < 0 || value_of(h.frame) == value))
                    n++;
            return n;
        }

        int reads_of(uint16_t reg) const
        {
            int n = 0;
            for (const Heard& h : heard)
                if (h.frame.size() >= 6 && h.frame[1] == 0x03 && reg_of(h.frame) == reg)
                    n++;
            return n;
        }

        // The blocks as flash: each at bank * 0x10000 + address, plus
        // 0x10000 for each time the addresses in its bank came round from
        // 0xFF80 to 0. A block its bank's erase came after is gone. What was
        // never written reads 0xFF. The blocks are the record; this is a
        // view of them.
        Bytes image() const
        {
            std::map<size_t, const Block*> at;
            std::map<int, int> last;
            std::map<int, size_t> wraps;
            size_t end = 0;
            size_t e   = 0;
            for (const Block& b : blocks)
            {
                for (; e < m_erase_log.size() && m_erase_log[e].first <= b.seq; e++)
                {
                    const int bank = m_erase_log[e].second;
                    last.erase(bank);
                    wraps.erase(bank);
                    for (auto i = at.begin(); i != at.end();)
                        i = i->second->bank == bank ? at.erase(i) : std::next(i);
                }
                auto l = last.find(b.bank);
                if (l != last.end() && l->second == 0xFF80 && b.address == 0)
                    wraps[b.bank]++;
                last[b.bank] = b.address;
                const size_t where = (size_t)b.bank * 0x10000 + wraps[b.bank] * 0x10000 + b.address;
                at[where] = &b;
            }
            for (; e < m_erase_log.size(); e++)
            {
                const int bank = m_erase_log[e].second;
                for (auto i = at.begin(); i != at.end();)
                    i = i->second->bank == bank ? at.erase(i) : std::next(i);
            }
            for (const auto& [where, b] : at)
                end = std::max(end, where + b->data.size());
            Bytes out(end, 0xFF);
            for (const auto& [where, b] : at)
                std::copy(b->data.begin(), b->data.end(), out.begin() + (ptrdiff_t)where);
            return out;
        }

        // What register 1991 reads.
        uint16_t packets() const
        {
            return packets_override >= 0 ? (uint16_t)packets_override : (uint16_t)m_packets.size();
        }

        static uint16_t reg_of(const Bytes& f) { return (uint16_t)(f[2] << 8 | f[3]); }
        static uint16_t value_of(const Bytes& f) { return (uint16_t)(f[4] << 8 | f[5]); }

    private:
        bool m_dead = false;
        int64_t m_quiet_until = 0;

        // This bootloader session: from the jump, or from power_on.
        bool m_session = false;
        bool m_began_interrupted = false;
        bool m_resume_pending = false;  // a resumed session's first block, not yet heard
        bool m_seen_init   = false;
        bool m_seen_start  = false;
        bool m_seen_erase  = false;
        bool m_seen_blocks = false;
        bool m_new_section = true;   // a bank chosen, or an erase, since the last block
        int m_last_address = -1;
        int m_bank = 0;

        // Blocks since the last erase, as (bank, address); and each erase,
        // as (blocks before it, bank).
        std::set<std::pair<int, int>> m_packets;
        std::vector<std::pair<int, int>> m_erase_log;

        void begin_session()
        {
            m_session           = true;
            m_began_interrupted = status == kStatusInterrupted || status == kStatusInterrupted2;
            m_resume_pending    = m_began_interrupted;
            m_seen_init   = false;
            m_seen_start  = false;
            m_seen_erase  = false;
            m_seen_blocks = false;
            m_new_section  = true;
            m_last_address = -1;
            m_bank = 0;
        }

        static std::string hex4(unsigned v)
        {
            static const char* digits = "0123456789ABCDEF";
            std::string s = "0x";
            for (int shift = 12; shift >= 0; shift -= 4)
                s += digits[(v >> shift) & 0xF];
            return s;
        }

        static std::string describe(const Bytes& f)
        {
            const std::string to = f[0] == 255 ? " to 255" : "";
            if (f[1] == 0x03 && f.size() >= 6)
                return "read " + std::to_string(value_of(f)) + " at " + std::to_string(reg_of(f)) + to;
            if (f[1] == 0x06 && f.size() >= 6)
                return "write " + std::to_string(reg_of(f)) + " = " + hex4(value_of(f)) + to;
            if (f[1] == 0x10 && f.size() >= 4)
                return "block at " + hex4(reg_of(f)) + to;
            return "function " + std::to_string(f[1]) + to;
        }

        ModbusFault* match_fault(const Bytes& f)
        {
            using Kind = ModbusFault::Kind;
            const Kind kind = f[1] == 0x03 ? Kind::Read : f[1] == 0x06 ? Kind::Write : f[1] == 0x10 ? Kind::Block : Kind::Any;
            for (ModbusFault& x : faults)
            {
                if (x.kind != Kind::Any && x.kind != kind)
                    continue;
                if (f.size() >= 4)
                {
                    const int reg = reg_of(f);
                    if (kind == Kind::Block)
                    {
                        if (x.bank >= 0 && x.bank != m_bank)
                            continue;
                        if (x.address >= 0 && x.address != reg)
                            continue;
                    }
                    else
                    {
                        if (x.reg >= 0 && x.reg != reg)
                            continue;
                        if (x.value >= 0 && (f.size() < 6 || x.value != value_of(f)))
                            continue;
                    }
                }
                const int n = x.matched++;
                if (n < x.skip)
                    continue;
                if (x.times >= 0 && n >= x.skip + x.times)
                    continue;
                return &x;
            }
            return nullptr;
        }

        static void apply(const ModbusFault& x, const Bytes& f, ModbusAnswer& a)
        {
            using Action = ModbusFault::Action;
            switch (x.action)
            {
            case Action::Garble:
                a.garble = true;
                break;
            case Action::WrongId:
                a.reply[0] = (uint8_t)(a.reply[0] + 1);
                break;
            case Action::WrongValue:
                // A write's high byte, which ISP compares on both transports:
                // over TCP it does not compare the low one (common.cpp:3002).
                if (f[1] == 0x06 && a.reply.size() >= 6)
                    a.reply[4] ^= 0x01;
                else if (f[1] == 0x03 && a.reply.size() >= 5)
                {
                    a.reply[2] = (uint8_t)(a.reply[2] - 2);
                    a.reply.resize(a.reply.size() - 2);
                }
                else if (f[1] == 0x10 && a.reply.size() >= 6)
                    a.reply[5] = 0x40;
                break;
            case Action::Exception:
                a.reply = { f[0], (uint8_t)(f[1] | 0x80), x.code };
                break;
            case Action::Late:
                a.delay_ms += x.delay_ms;
                break;
            case Action::Deaf:
            case Action::Silence:
                break;
            }
        }

        static bool is_update_register(uint16_t reg) { return reg >= 1992 && reg <= 1999; }

        uint16_t register_now(uint16_t reg) const
        {
            if (reg == kRegStatus)
                return status;
            if (reg == kRegPackets)
                return packets();
            const auto& regs = is_update_register(reg) ? update_registers
                             : mode == Mode::Bootloader ? boot_registers
                                                        : app_registers;
            auto r = regs.find(reg);
            return r == regs.end() ? 0 : r->second;
        }

        // The reads ISP makes, as (first register, count).
        static bool isp_reads(uint16_t reg, uint16_t count, uint8_t unit)
        {
            static const std::pair<uint16_t, uint16_t> reads[] = {
                { 0, 40 },             // Flash_Modebus_Device's identity (ComWriter.cpp:405-448)
                { 0, 18 },             // UpdataDeviceInformation (:1983-2209)
                { 0, 100 },            // TFTP's read before it flashes (TFTPServer.cpp:2092)
                { kRegBoot11, 1 },     // the wait for the bootloader, the chip check (:1487-1503, 2539-2582)
                { kRegStatus, 1 },     // interrupted? (:857, 2614)
                { kRegPackets, 1 },    // where to resume (:831-836)
                { kRegMd5, 4 },        // the MD5 kept (:868)
                { 1994, 6 },           // an ESP32's resume, by the other map (:1442)
                { kRegCpu, 1 },        // the ARM thread's retry delay (:2827)
                { kRegEspStatus, 1 },  // an ESP32's end (:1770)
            };
            for (const auto& [r, c] : reads)
                if (r == reg && c == count)
                    return true;

            // The wake-up read before a block's last three tries, to 255
            // (:1013).
            return unit == 255 && reg == 1 && count == 1;
        }

        Bytes read(const Bytes& f, std::string& not_isp)
        {
            if (f.size() != 6)
            {
                not_isp = "a read of " + std::to_string(f.size()) + " bytes";
                return { f[0], 0x83, 3 };
            }
            const uint16_t reg   = reg_of(f);
            const uint16_t count = value_of(f);
            if (!isp_reads(reg, count, f[0]))
                not_isp = "ISP never reads " + std::to_string(count) + " at " + std::to_string(reg);
            if (count == 0 || count > 125)
                return { f[0], 0x83, 3 };

            Bytes r = { f[0], 0x03, (uint8_t)(count * 2) };
            for (uint16_t i = 0; i < count; i++)
            {
                const uint16_t v = register_now((uint16_t)(reg + i));
                r.push_back((uint8_t)(v >> 8));
                r.push_back((uint8_t)(v & 0xFF));
            }
            return r;
        }

        // Why ISP would not make this write; empty when it would.
        static std::string isp_write(uint16_t reg, uint16_t value, uint8_t unit)
        {
            if (reg == kRegCommand && value == kCmdQuiet)
                return unit == 255 ? "" : "0x0455, which ISP writes only to 255";
            if (reg == kRegCommand)
            {
                if (value == kCmdJump || value == kCmdErase || value == kCmdStart || value == kCmdEnd || value == kCmdAfter0)
                    return "";
                return "ISP never writes " + hex4(value) + " to register 16";
            }
            if (reg == kRegSection)
                return value < 64 ? "" : "section " + std::to_string(value) + ", past any ISP sends";
            if (reg == kRegBank)
                return value <= 1 ? "" : "ISP writes only 0 or 1 to register 33";
            if (reg >= kRegMd5 && reg <= kRegSizeLow)
                return "";
            return "ISP never writes register " + std::to_string(reg) + " of a device";
        }

        Bytes write(const Bytes& f, int64_t now_ms, bool acts, std::string& not_isp, bool& reply)
        {
            if (f.size() != 6)
            {
                not_isp = "a write of " + std::to_string(f.size()) + " bytes";
                return { f[0], 0x86, 3 };
            }
            const uint16_t reg   = reg_of(f);
            const uint16_t value = value_of(f);
            not_isp = isp_write(reg, value, f[0]);
            if (!acts || (reg == kRegCommand && value == kCmdQuiet))
                return f;

            if (is_update_register(reg))
            {
                update_registers[reg] = value;
                return f;
            }

            if (mode == Mode::Application)
            {
                if (reg == kRegCommand && value == kCmdJump)
                {
                    // Echoed, then the jump: silent while it restarts.
                    jumps++;
                    mode = Mode::Bootloader;
                    m_quiet_until = now_ms + jump_ms;
                    begin_session();
                    m_seen_init = true;
                    return f;
                }
                app_registers[reg] = value;
                return f;
            }

            if (reg == kRegCommand)
            {
                switch (value)
                {
                case kCmdJump:
                    m_seen_init = true;
                    reply = echoes_init_and_erase;
                    break;
                case kCmdErase:
                    if (!m_seen_init && not_isp.empty())
                        not_isp = "an erase before any 0x7F";
                    erase();
                    reply = echoes_init_and_erase;
                    m_quiet_until = now_ms + erase_ms;
                    break;
                case kCmdStart:
                    m_seen_start = true;
                    status = kStatusInterrupted;
                    break;
                case kCmdEnd:
                    // Before any block, ISP's "reset": echoed, nothing else.
                    if (!m_seen_blocks)
                        break;
                    status = 0;
                    if (end_restarts)
                    {
                        mode = Mode::Application;
                        m_session = false;
                        m_quiet_until = now_ms + restart_ms;
                    }
                    break;
                default:
                    break;
                }
                return f;
            }

            if (reg == kRegSection || reg == kRegBank)
            {
                m_bank = value;
                m_new_section = true;
            }
            boot_registers[reg] = value;
            return f;
        }

        void erase()
        {
            erases++;
            m_seen_erase  = true;
            m_new_section = true;
            m_packets.clear();
            m_erase_log.push_back({ (int)blocks.size(), m_bank });
        }

        Bytes block(const Bytes& f, std::string& not_isp, bool& reply)
        {
            // ISP's block: address, 00 80, 80, 128 bytes (common.cpp:4225-4241).
            const bool framed = f.size() == 7 + (size_t)kBlockBytes && f[4] == (uint8_t)(kBlockBytes >> 8)
                             && f[5] == (uint8_t)(kBlockBytes & 0xFF) && f[6] == (uint8_t)kBlockBytes;
            if (!framed)
            {
                not_isp = "a block not framed as ISP's: 00 80, 80 and 128 bytes";
                reply = false;
                return {};
            }
            if (mode == Mode::Application)
            {
                // A running application takes no blocks.
                not_isp = "a block to the application, before the jump";
                reply = false;
                return {};
            }

            const uint16_t address = reg_of(f);
            if (!m_seen_start && !m_began_interrupted)
                not_isp = "a block before 0x1F";
            else if (address % kBlockBytes != 0)
                not_isp = "a block at " + hex4(address) + ", not a multiple of 128";
            else if (m_new_section || m_last_address < 0)
            {
                // Only a resume's first block may start past 0, and only
                // where 1991 says (:885, 976-981).
                const bool resuming = m_resume_pending && !m_seen_erase
                                   && address == (uint16_t)(packets() * kBlockBytes);
                if (address != 0 && !resuming)
                    not_isp = "a first block at " + hex4(address) + ", not 0";
            }
            else if (address != m_last_address && address != ((m_last_address + kBlockBytes) & 0xFFFF))
                not_isp = "a block at " + hex4(address) + " after one at " + hex4((unsigned)m_last_address);

            Block b;
            b.seq     = (int)blocks.size();
            b.bank    = m_bank;
            b.address = address;
            b.data.assign(f.begin() + 7, f.end());
            blocks.push_back(b);

            m_new_section    = false;
            m_resume_pending = false;
            m_last_address   = address;
            m_seen_blocks    = true;
            m_packets.insert({ m_bank, address });

            return { f[0], 0x10, f[2], f[3], f[4], f[5] };
        }
    };
}
