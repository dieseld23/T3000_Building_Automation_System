#pragma once

// A COM port, opened for a serial scan.
//
// It is a SerialLine, so all it can send is a serial::ScanFrame: the range
// query or the read of registers 0-9 (serial/rtu.h). There is no way to hand
// it other bytes, so a scan over a real port stays read-only by the same
// shape as the scripted line in the tests.
//
// It opens the port the way T3000 does (ModbusDllforVc/common.cpp:5030-5098):
//
//   - \\.\ and the name, exclusive, as CreateFile's own (:5041-5047). T3000
//     writes COM1-COM9 as "COMn:"; the \\.\ form opens those too, and names
//     that are not COMn, such as com0com's CNCA0.
//   - 8 data bits, no parity, 1 stop bit (:5073-5076, with the defaults at
//     :33-35), and the rate the scan asks for. Flow control, DTR and RTS are
//     left as the driver has them, as T3000 leaves them: some RS485
//     adapters switch direction on RTS, and set by the driver.
//   - The buffers purged before each frame is sent (:7257-7258), so a late
//     reply to the last frame is not read as the answer to this one.
//
// And differs from it where T3000 goes wrong or says nothing:
//
//   - Only a plain name (letters, digits, underscore) is opened, and the
//     route only passes one the registry lists as a serial port.
//   - A port another program holds, one that has gone, and one that stops
//     answering mid-scan each get their own message. T3000 says "Cannot open
//     the COM Port" for all of them (TStatScanner.cpp:729-731).
//   - A reply is read until the line has been quiet for kQuietGapMs, T3000's
//     ReadIntervalTimeout (:5083), or the scan's timeout, rather than for a
//     fixed 13 bytes (:7291), so a second device's reply is not cut off.
//   - An adapter that echoes what it sends puts the frame in front of the
//     reply. The echo is taken off, where T3000 counts an exact echo as no
//     reply (:7374-7379) and an echo with a reply after it as garbage.
//   - fAbortOnError is cleared, so one framing error - a device at another
//     rate - does not stop every read after it.

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

#include "serial_scan.h"

namespace t5000::discovery
{
    // How many of the first bytes of `got` are the frame just sent, echoed:
    // all of `sent` when `got` starts with it, all of `got` when it is only
    // the start of it, and 0 otherwise - including when `got` is longer than
    // `sent` and is one whole frame by its CRC, a reply that only begins like
    // the frame.
    size_t leading_echo(const uint8_t* got, size_t length, const std::vector<uint8_t>& sent);

    // What the operator is told when a port does not open, from the Windows
    // error code.
    std::string open_error_text(const std::string& port, unsigned long code);

    class ComPortLine : public SerialLine
    {
    public:
        // After the last byte, how long the line must stay quiet before a
        // reply is taken as complete. T3000's ReadIntervalTimeout.
        static constexpr int kQuietGapMs = 160;

        ComPortLine() = default;
        ~ComPortLine() override;
        ComPortLine(const ComPortLine&) = delete;
        ComPortLine& operator=(const ComPortLine&) = delete;

        // Opens the port at `baud`. False, with a message for the operator,
        // when it cannot: a name that is not plain, a port that is in use or
        // gone, or one that is not a serial port.
        bool open(const std::string& name, int baud, std::string& error);
        void close();
        bool is_open() const { return m_handle != nullptr; }

        bool set_rate(int baud, std::string& error) override;
        bool send(const serial::ScanFrame& frame, std::string& error) override;
        int  receive(uint8_t* buffer, int capacity, int timeout_ms, std::string& error) override;

    private:
        bool configure(int baud, std::string& error);
        std::string failed(const char* doing) const;

        void*       m_handle = nullptr;   // a HANDLE
        std::string m_name;

        // The last frame sent, until the reply to it has been read.
        std::vector<uint8_t> m_sent;
    };
}
