#include "ports.h"

#include <windows.h>
#include <string.h>

#include <algorithm>

namespace t5000::serial
{
    namespace
    {
        std::string utf8_from_wide(const wchar_t* w, int length)
        {
            if (length <= 0)
                return std::string();
            const int n = WideCharToMultiByte(CP_UTF8, 0, w, length, nullptr, 0, nullptr, nullptr);
            if (n <= 0)
                return std::string();
            std::string out((size_t)n, '\0');
            WideCharToMultiByte(CP_UTF8, 0, w, length, &out[0], n, nullptr, nullptr);
            return out;
        }

        // "COM12" -> 12. 0 for anything else, including "COM0" and "COM" alone.
        int com_number(const std::string& name)
        {
            if (name.size() < 4 || _strnicmp(name.c_str(), "COM", 3) != 0)
                return 0;
            int n = 0;
            for (size_t i = 3; i < name.size(); i++)
            {
                if (name[i] < '0' || name[i] > '9' || n > 100000)
                    return 0;
                n = n * 10 + (name[i] - '0');
            }
            return n;
        }

        // The device names of common USB-serial drivers, after "\Device\".
        // T3000 looks for USBSER alone (global_function.cpp:1024), which is
        // Windows' own driver for CDC adapters; the FTDI adapters RS485
        // installers mostly carry show as VCP.
        bool is_usb_driver(const std::string& device)
        {
            const char* const prefixes[] = { "USBSER", "VCP", "Silabser", "ProlificSerial", "CH341SER" };

            std::string leaf = device;
            const size_t slash = leaf.find_last_of('\\');
            if (slash != std::string::npos)
                leaf.erase(0, slash + 1);

            for (const char* p : prefixes)
            {
                const size_t n = strlen(p);
                if (leaf.size() >= n && _strnicmp(leaf.c_str(), p, n) == 0)
                    return true;
            }
            return false;
        }
    }

    std::vector<Port> ports_from_values(const std::vector<std::pair<std::string, std::string>>& values)
    {
        std::vector<Port> out;
        for (const auto& v : values)
        {
            if (v.second.empty())
                continue;

            Port p;
            p.device = v.first;
            p.name   = v.second;
            p.number = com_number(p.name);
            p.usb    = is_usb_driver(p.device);
            out.push_back(p);
        }

        std::stable_sort(out.begin(), out.end(), [](const Port& a, const Port& b) {
            if ((a.number == 0) != (b.number == 0))
                return a.number != 0;
            if (a.number != b.number)
                return a.number < b.number;
            return a.name < b.name;
        });
        return out;
    }

    std::vector<Port> list_ports(std::string& error)
    {
        error.clear();

        HKEY key = nullptr;
        const LONG opened = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DEVICEMAP\\SERIALCOMM", 0,
                                          KEY_QUERY_VALUE, &key);
        if (opened == ERROR_FILE_NOT_FOUND)
            return {};
        if (opened != ERROR_SUCCESS)
        {
            error = "the serial ports could not be listed (registry error " + std::to_string(opened) + ")";
            return {};
        }

        std::vector<std::pair<std::string, std::string>> values;
        for (DWORD i = 0;; i++)
        {
            wchar_t name[256] = {};
            wchar_t data[256] = {};
            DWORD name_chars = 256;
            DWORD data_bytes = sizeof(data) - sizeof(wchar_t);
            DWORD type = 0;

            const LONG r = RegEnumValueW(key, i, name, &name_chars, nullptr, &type, (LPBYTE)data, &data_bytes);
            if (r == ERROR_NO_MORE_ITEMS)
                break;
            if (r != ERROR_SUCCESS)
            {
                // One value too long for the buffers is skipped rather than
                // losing the whole list over it.
                if (r == ERROR_MORE_DATA)
                    continue;
                error = "the serial ports could not all be listed (registry error " + std::to_string(r) + ")";
                break;
            }
            if (type != REG_SZ)
                continue;

            int data_chars = (int)(data_bytes / sizeof(wchar_t));
            while (data_chars > 0 && data[data_chars - 1] == L'\0')
                data_chars--;

            values.push_back({ utf8_from_wide(name, (int)name_chars), utf8_from_wide(data, data_chars) });
        }

        RegCloseKey(key);
        return ports_from_values(values);
    }
}
