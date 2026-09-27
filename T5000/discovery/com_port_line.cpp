#include "com_port_line.h"

#include <windows.h>

#include <string.h>

#include <algorithm>

#include "../serial/ports.h"

namespace t5000::discovery
{
    namespace
    {
        std::string system_message(unsigned long code)
        {
            char text[512] = {};
            const DWORD n = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code,
                                           0, text, (DWORD)sizeof(text), nullptr);
            std::string s(text, n);
            while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '.'))
                s.pop_back();
            if (s.empty())
                s = "Windows error " + std::to_string(code);
            return s;
        }

        HANDLE handle_of(void* h)
        {
            return static_cast<HANDLE>(h);
        }
    }

    size_t leading_echo(const uint8_t* got, size_t length, const std::vector<uint8_t>& sent)
    {
        if (sent.empty() || length == 0)
            return 0;
        // In brackets: windows.h defines min.
        const size_t n = (std::min)(length, sent.size());
        if (memcmp(got, sent.data(), n) != 0)
            return 0;

        // Longer than the frame, and one whole frame by its own CRC: a reply
        // that happens to begin with the bytes sent - a device whose serial
        // starts with the query's last bytes - not an echo with a reply
        // after it, which does not check out as one frame.
        if (length > sent.size() && serial::crc16(got, length) == 0)
            return 0;
        return n;
    }

    std::string open_error_text(const std::string& port, unsigned long code)
    {
        switch (code)
        {
        case ERROR_ACCESS_DENIED:
        case ERROR_SHARING_VIOLATION:
            return port + " is open in another program - T3000, a terminal, or another copy of T5000. Close it "
                          "there and scan again.";
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
            return port + " is not there. A USB adapter may have been unplugged since the list was read; reload the "
                          "page to list the ports again.";
        case ERROR_GEN_FAILURE:
        case ERROR_DEVICE_NOT_CONNECTED:
        case ERROR_BAD_COMMAND:
            return port + " did not respond to Windows. Its adapter may have been unplugged.";
        default:
            return port + " could not be opened: " + system_message(code) + " (Windows error " +
                   std::to_string(code) + ").";
        }
    }

    ComPortLine::~ComPortLine()
    {
        close();
    }

    void ComPortLine::close()
    {
        if (m_handle)
            CloseHandle(handle_of(m_handle));
        m_handle = nullptr;
        m_sent.clear();
    }

    std::string ComPortLine::failed(const char* doing) const
    {
        const DWORD code = GetLastError();
        if (code == ERROR_GEN_FAILURE || code == ERROR_DEVICE_NOT_CONNECTED || code == ERROR_BAD_COMMAND ||
            code == ERROR_OPERATION_ABORTED || code == ERROR_ACCESS_DENIED)
            return m_name + " stopped responding while " + doing + " - was its adapter unplugged? (" +
                   system_message(code) + ")";
        return m_name + " failed while " + doing + ": " + system_message(code) + " (Windows error " +
               std::to_string(code) + ")";
    }

    bool ComPortLine::open(const std::string& name, int baud, std::string& error)
    {
        close();
        if (!serial::is_plain_port_name(name))
        {
            error = "\"" + name + "\" is not a serial port name T5000 opens.";
            return false;
        }

        const std::string path = "\\\\.\\" + name;
        HANDLE h = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE)
        {
            error = open_error_text(name, GetLastError());
            return false;
        }
        m_handle = h;
        m_name   = name;

        // Whatever opens by a name the registry lists as a serial port should
        // be one, but a port that has no line settings is not something to
        // send frames to.
        DCB probe = {};
        probe.DCBlength = sizeof(probe);
        if (GetFileType(h) != FILE_TYPE_CHAR || !GetCommState(h, &probe))
        {
            close();
            error = name + " opened, but is not a serial port, so nothing was sent to it.";
            return false;
        }

        // Not fatal: a virtual port may keep its own buffers.
        SetupComm(h, 4096, 4096);

        if (!configure(baud, error))
        {
            close();
            return false;
        }

        // Reads return at once with whatever has arrived; receive() does the
        // waiting. Writes give up as T3000's do (:5086-5087).
        COMMTIMEOUTS t = {};
        t.ReadIntervalTimeout         = MAXDWORD;
        t.ReadTotalTimeoutMultiplier  = 0;
        t.ReadTotalTimeoutConstant    = 0;
        t.WriteTotalTimeoutMultiplier = 20;
        t.WriteTotalTimeoutConstant   = 200;
        if (!SetCommTimeouts(h, &t))
        {
            error = failed("its timeouts were set");
            close();
            return false;
        }

        PurgeComm(h, PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR);
        return true;
    }

    bool ComPortLine::configure(int baud, std::string& error)
    {
        DCB dcb = {};
        dcb.DCBlength = sizeof(dcb);
        if (!GetCommState(handle_of(m_handle), &dcb))
        {
            error = failed("its settings were read");
            return false;
        }
        dcb.BaudRate      = (DWORD)baud;
        dcb.ByteSize      = 8;
        dcb.Parity        = NOPARITY;
        dcb.StopBits      = ONESTOPBIT;
        dcb.fBinary       = TRUE;
        dcb.fParity       = FALSE;
        dcb.fAbortOnError = FALSE;
        if (!SetCommState(handle_of(m_handle), &dcb))
        {
            error = m_name + " would not take " + std::to_string(baud) + " baud, 8 data bits, no parity, 1 stop bit: " +
                    system_message(GetLastError());
            return false;
        }
        return true;
    }

    bool ComPortLine::set_rate(int baud, std::string& error)
    {
        if (!m_handle)
        {
            error = "the port is not open";
            return false;
        }
        if (!configure(baud, error))
            return false;
        PurgeComm(handle_of(m_handle), PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR);
        m_sent.clear();
        return true;
    }

    bool ComPortLine::send(const serial::ScanFrame& frame, std::string& error)
    {
        if (!m_handle)
        {
            error = "the port is not open";
            return false;
        }
        if (frame.empty())
        {
            error = "an empty frame is not sent";
            return false;
        }

        HANDLE h = handle_of(m_handle);
        DWORD errors = 0;
        COMSTAT stat = {};
        if (!ClearCommError(h, &errors, &stat))
        {
            error = failed("a frame was sent");
            return false;
        }
        PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);

        const std::vector<uint8_t>& bytes = frame.bytes();
        DWORD written = 0;
        if (!WriteFile(h, bytes.data(), (DWORD)bytes.size(), &written, nullptr))
        {
            error = failed("a frame was sent");
            return false;
        }
        // A write to an RS485 adapter does not wait on anything, unless the
        // port has hardware flow control on and nothing raises CTS. T5000
        // leaves flow control as the driver has it, as T3000 does.
        if (written != bytes.size())
        {
            error = m_name + " took " + std::to_string(written) + " of the " + std::to_string(bytes.size()) +
                    " bytes of a frame before its write timed out. Its flow control may be waiting on a CTS "
                    "signal nothing raises: check the port's settings in Device Manager.";
            return false;
        }
        m_sent = bytes;
        return true;
    }

    int ComPortLine::receive(uint8_t* buffer, int capacity, int timeout_ms, std::string& error)
    {
        if (!m_handle)
        {
            error = "the port is not open";
            return -1;
        }
        if (capacity <= 0)
            return 0;

        HANDLE h = handle_of(m_handle);
        const ULONGLONG start    = GetTickCount64();
        const ULONGLONG deadline = start + (ULONGLONG)(timeout_ms > 0 ? timeout_ms : 0);
        ULONGLONG last_byte = 0;
        int got = 0;

        for (;;)
        {
            DWORD errors = 0;
            COMSTAT stat = {};
            if (!ClearCommError(h, &errors, &stat))
            {
                error = failed("a reply was read");
                return -1;
            }

            if (stat.cbInQue > 0 && got < capacity)
            {
                const DWORD want = std::min<DWORD>(stat.cbInQue, (DWORD)(capacity - got));
                DWORD read = 0;
                if (!ReadFile(h, buffer + got, want, &read, nullptr))
                {
                    error = failed("a reply was read");
                    return -1;
                }
                if (read > 0)
                {
                    got += (int)read;
                    last_byte = GetTickCount64();
                }
            }

            const ULONGLONG now = GetTickCount64();
            if (got >= capacity || now >= deadline)
                break;

            // Quiet for kQuietGapMs after the last byte: the reply is over.
            // Not while all that has come is the frame's own echo, which an
            // adapter sends back at once, before any device can answer.
            const bool only_echo = got > 0 && leading_echo(buffer, (size_t)got, m_sent) == (size_t)got;
            if (got > 0 && !only_echo && now - last_byte >= (ULONGLONG)kQuietGapMs)
                break;

            Sleep(2);
        }

        const size_t echo = leading_echo(buffer, (size_t)got, m_sent);
        if (echo > 0)
        {
            memmove(buffer, buffer + echo, (size_t)got - echo);
            got -= (int)echo;
        }
        m_sent.clear();
        return got;
    }
}
