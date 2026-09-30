#pragma once

// Synthetic bootloaders on one serial line, as ISP reaches a device over a
// COM port: Modbus RTU, "id fc data" and a CRC, low byte first. For the tests
// of T5000's serial flash (F3), and of several devices one after another
// (F6).
//
// The line is a timeline. send() puts a frame on it at a time the test
// chooses; each device that hears it answers, and its reply arrives after
// the device's delay. receive() takes what has arrived by a given time. A
// test's clock jumps from one arrival to the next and never waits.
//
// Every frame sent is kept. The devices judge what they hear, and a frame
// with a bad CRC, which no device hears and ISP never sends, is counted
// here: frames_not_isp() is both.
//
// The CRC is modbus_crc.h's, never serial::crc16, so a mistake in T5000's
// shows up as devices that do not answer.

#include <stdint.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "fake_bootloader.h"
#include "modbus_crc.h"

namespace t5000::testing
{
    class FakeRtuBus
    {
    public:
        struct Sent
        {
            int64_t at_ms = 0;
            Bytes bytes;
        };

        std::vector<FakeBootloader> devices;

        // The line's rate; 0 until one is set. A device with a rate of its
        // own hears nothing at another: its reply would be noise, and here
        // it is silence.
        int rate = 0;

        std::vector<Sent> sent;
        int bad_crc_frames = 0;

        void set_rate(int baud) { rate = baud; }

        FakeBootloader* device(uint8_t id)
        {
            for (FakeBootloader& d : devices)
                if (d.id == id)
                    return &d;
            return nullptr;
        }

        // The master sends a whole frame, CRC included.
        void send(const Bytes& frame, int64_t now_ms)
        {
            sent.push_back({ now_ms, frame });
            if (!line_crc_ok(frame))
            {
                bad_crc_frames++;
                return;
            }

            const Bytes pdu(frame.begin(), frame.end() - 2);
            for (FakeBootloader& d : devices)
            {
                const int b = d.baud();
                if (b != 0 && rate != 0 && b != rate)
                    continue;

                const ModbusAnswer a = d.hear(pdu, now_ms);
                if (a.silent)
                    continue;
                Bytes reply = a.reply;
                add_line_crc(reply);
                if (a.garble)
                    reply.back() ^= 0x5A;
                m_on_the_way.push_back({ now_ms + a.delay_ms, reply });
            }
            std::stable_sort(m_on_the_way.begin(), m_on_the_way.end(),
                             [](const auto& x, const auto& y) { return x.first < y.first; });
        }

        // What has arrived by now_ms, in the order it came, taken off the
        // line. Replies that arrive together run into each other, as they
        // would on a real bus.
        Bytes receive(int64_t now_ms)
        {
            Bytes out;
            auto i = m_on_the_way.begin();
            for (; i != m_on_the_way.end() && i->first <= now_ms; ++i)
                out.insert(out.end(), i->second.begin(), i->second.end());
            m_on_the_way.erase(m_on_the_way.begin(), i);
            return out;
        }

        // When the next reply arrives; -1 when none is on its way.
        int64_t next_arrival() const { return m_on_the_way.empty() ? -1 : m_on_the_way.front().first; }

        // Drops what has arrived by now_ms, as PurgeComm does before each of
        // ISP's requests (common.cpp:1709, 2729, 4256, 9177). A reply still
        // on its way arrives later.
        void purge(int64_t now_ms) { receive(now_ms); }

        int frames_not_isp() const
        {
            int n = bad_crc_frames;
            for (const FakeBootloader& d : devices)
                n += d.frames_not_isp();
            return n;
        }

    private:
        std::vector<std::pair<int64_t, Bytes>> m_on_the_way;
    };
}
