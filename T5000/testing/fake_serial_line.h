#pragma once

// A serial line, scripted, for tests of the serial scan.
//
// The devices on it read each frame from its bytes, check its CRC with their
// own arithmetic, and answer only the two frames a Temco device answers to a
// scan: the range query and the read of registers 0-9. Anything else goes
// unanswered, as it would on a real line. Nothing here builds or reads a
// frame with serial/rtu.h, so a fault there shows up as devices that do not
// answer, not as a test that agrees with the fault.
//
// Every frame sent is kept, and every receive counted, so a test can say that
// nothing was sent, or that nothing was even listened for. An empty inbox
// times out at once, so tests never wait on a clock.

#include <string.h>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include "../discovery/serial_scan.h"

namespace t5000::testing
{
    using SerialBytes = std::vector<uint8_t>;

    // CRC-16/MODBUS, table-driven where serial::crc16 works bit by bit, so the
    // two cannot share a mistake. The self-test checks this one against the
    // standard's check value.
    inline uint16_t line_crc(const uint8_t* data, size_t length)
    {
        static const std::array<uint16_t, 256> table = [] {
            std::array<uint16_t, 256> t{};
            for (int i = 0; i < 256; i++)
            {
                uint16_t c = (uint16_t)i;
                for (int k = 0; k < 8; k++)
                    c = (c & 1) ? (uint16_t)((c >> 1) ^ 0xA001) : (uint16_t)(c >> 1);
                t[i] = c;
            }
            return t;
        }();

        uint16_t crc = 0xFFFF;
        for (size_t i = 0; i < length; i++)
            crc = (uint16_t)((crc >> 8) ^ table[(crc ^ data[i]) & 0xFF]);
        return crc;
    }

    inline void add_line_crc(SerialBytes& b)
    {
        const uint16_t crc = line_crc(b.data(), b.size());
        b.push_back((uint8_t)(crc & 0xFF));
        b.push_back((uint8_t)(crc >> 8));
    }

    inline bool line_crc_ok(const SerialBytes& b)
    {
        if (b.size() < 3)
            return false;
        const uint16_t crc = line_crc(b.data(), b.size() - 2);
        return b[b.size() - 2] == (uint8_t)(crc & 0xFF) && b[b.size() - 1] == (uint8_t)(crc >> 8);
    }

    // True when the bytes are one of the two frames a serial scan may send,
    // judged from the bytes alone:
    //   FF 19 hi lo CRC, with 1 <= lo <= hi <= 254, or
    //   id 03 00 00 00 0A CRC, with 1 <= id <= 254.
    inline bool is_scan_frame(const SerialBytes& b)
    {
        if (!line_crc_ok(b))
            return false;
        if (b.size() == 6 && b[0] == 0xFF && b[1] == 0x19)
            return b[3] >= 1 && b[3] <= b[2] && b[2] <= 254;
        if (b.size() == 8 && b[1] == 0x03 && b[2] == 0 && b[3] == 0 && b[4] == 0 && b[5] == 10)
            return b[0] >= 1 && b[0] <= 254;
        return false;
    }

    // A Temco device on the line.
    struct SerialDevice
    {
        uint8_t  id       = 1;
        uint32_t serial   = 0;
        int      firmware = 538;   // tenths: registers 5 and 4 as one number
        uint8_t  product  = 88;
        uint8_t  hardware = 26;

        // What register 6 says. 0 is the id it answers on.
        uint8_t reported_id = 0;

        // An old Tstat's version, 240-249, in register 4 alone. 0 for none.
        uint16_t old_version = 0;

        // Older firmware answers the range query in five bytes, not nine.
        bool short_range_reply = false;

        // A device that answers the range query but not the read.
        bool answers_identity = true;

        // Answers the read with this Modbus exception instead. 0 for none.
        uint8_t identity_exception = 0;

        // How many of its next range replies, and reads, arrive with a
        // corrupted CRC before it answers cleanly. -1 for every one.
        int garble_range_replies    = 0;
        int garble_identity_replies = 0;

        SerialBytes range_reply()
        {
            SerialBytes b = { 0xFF, 0x19, id };
            if (!short_range_reply)
            {
                b.push_back((uint8_t)(serial & 0xFF));
                b.push_back((uint8_t)((serial >> 8) & 0xFF));
                b.push_back((uint8_t)((serial >> 16) & 0xFF));
                b.push_back((uint8_t)((serial >> 24) & 0xFF));
            }
            add_line_crc(b);
            return garble(b, garble_range_replies);
        }

        SerialBytes identity_reply()
        {
            SerialBytes b;
            if (identity_exception != 0)
            {
                b = { id, 0x83, identity_exception };
                add_line_crc(b);
                return b;
            }

            const uint16_t reg[10] = {
                (uint16_t)(serial & 0xFF),
                (uint16_t)((serial >> 8) & 0xFF),
                (uint16_t)((serial >> 16) & 0xFF),
                (uint16_t)((serial >> 24) & 0xFF),
                old_version != 0 ? old_version : (uint16_t)(firmware & 0xFF),
                old_version != 0 ? (uint16_t)7 : (uint16_t)(firmware >> 8),
                reported_id != 0 ? reported_id : id,
                product,
                hardware,
                0,
            };
            b = { id, 0x03, 20 };
            for (uint16_t r : reg)
            {
                b.push_back((uint8_t)(r >> 8));
                b.push_back((uint8_t)(r & 0xFF));
            }
            add_line_crc(b);
            return garble(b, garble_identity_replies);
        }

    private:
        static SerialBytes garble(SerialBytes b, int& remaining)
        {
            if (remaining == 0)
                return b;
            if (remaining > 0)
                remaining--;
            b[b.size() - 1] ^= 0x5A;
            return b;
        }
    };

    class FakeSerialLine : public discovery::SerialScanTransport
    {
    public:
        // When several devices answer at once: their replies one after the
        // other, or on top of each other, as a real bus collision is.
        enum class Collision
        {
            Concatenate,
            Overlay,
        };

        std::vector<SerialDevice> devices;
        Collision collision = Collision::Concatenate;

        // What arrives before anything is sent: another master, or MS/TP.
        SerialBytes chatter;

        // An MS/TP line: from this frame on, counting from 0, every frame
        // sent is answered with token frames. -1 for never.
        int mstp_from = -1;

        // The send, or the receive, counting from 0, at which the line
        // fails. -1 for never.
        int fail_send_at    = -1;
        int fail_receive_at = -1;

        // What happened.
        std::vector<SerialBytes> sent;
        std::vector<int> timeouts;   // each receive's timeout, in order
        int receives      = 0;
        int refused_empty = 0;

        bool send(const serial::ScanFrame& frame, std::string& error) override
        {
            if (frame.empty())
            {
                refused_empty++;
                error = "an empty frame";
                return false;
            }
            if (fail_send_at == (int)sent.size())
            {
                error = "the port was closed";
                return false;
            }
            sent.push_back(frame.bytes());
            m_inbox = answer(frame.bytes());
            return true;
        }

        int receive(uint8_t* buffer, int capacity, int timeout_ms, std::string& error) override
        {
            timeouts.push_back(timeout_ms);
            if (fail_receive_at == receives++)
            {
                error = "the adapter was unplugged";
                return -1;
            }

            const SerialBytes& from = m_listened ? m_inbox : chatter;
            m_listened = true;
            const int n = std::min((int)from.size(), capacity);
            if (n > 0)
                memcpy(buffer, from.data(), (size_t)n);
            m_inbox.clear();
            return n;
        }

        // Every frame sent, checked from its bytes. 0 when all of them were
        // scan frames.
        int frames_not_scan_frames() const
        {
            int bad = 0;
            for (const SerialBytes& b : sent)
                if (!is_scan_frame(b))
                    bad++;
            return bad;
        }

        // How many range queries, and reads of one id, were sent.
        int range_queries() const
        {
            int n = 0;
            for (const SerialBytes& b : sent)
                if (b.size() == 6 && b[0] == 0xFF)
                    n++;
            return n;
        }

        int reads_of(uint8_t id) const
        {
            int n = 0;
            for (const SerialBytes& b : sent)
                if (b.size() == 8 && b[0] == id && b[1] == 0x03)
                    n++;
            return n;
        }

        int queries_for_id(uint8_t id) const
        {
            int n = 0;
            for (const SerialBytes& b : sent)
                if (b.size() == 6 && b[0] == 0xFF && b[2] == id && b[3] == id)
                    n++;
            return n;
        }

    private:
        SerialBytes m_inbox;
        bool m_listened = false;

        SerialBytes combine(std::vector<SerialBytes> replies) const
        {
            if (replies.empty())
                return {};
            SerialBytes out = replies[0];
            for (size_t i = 1; i < replies.size(); i++)
            {
                const SerialBytes& r = replies[i];
                if (collision == Collision::Concatenate)
                {
                    out.insert(out.end(), r.begin(), r.end());
                    continue;
                }
                if (r.size() > out.size())
                    out.resize(r.size(), 0);
                for (size_t k = 0; k < r.size(); k++)
                    out[k] |= r[k];
            }
            return out;
        }

        SerialBytes answer(const SerialBytes& b)
        {
            if (mstp_from >= 0 && (int)sent.size() > mstp_from)
            {
                // Two token frames: preamble, type 0, destination, source,
                // no data, and a header CRC.
                return { 0x55, 0xFF, 0x00, 0x02, 0x01, 0x00, 0x00, 0x7C,
                         0x55, 0xFF, 0x00, 0x03, 0x02, 0x00, 0x00, 0x3B };
            }
            if (!line_crc_ok(b))
                return {};

            std::vector<SerialBytes> replies;
            if (b.size() == 6 && b[0] == 0xFF && b[1] == 0x19)
            {
                const uint8_t hi = b[2];
                const uint8_t lo = b[3];
                for (SerialDevice& d : devices)
                    if (d.id >= lo && d.id <= hi)
                        replies.push_back(d.range_reply());
            }
            else if (b.size() == 8 && b[1] == 0x03 && b[2] == 0 && b[3] == 0 && b[4] == 0 && b[5] == 10)
            {
                for (SerialDevice& d : devices)
                    if (d.id == b[0] && d.answers_identity)
                        replies.push_back(d.identity_reply());
            }
            return combine(replies);
        }
    };
}
