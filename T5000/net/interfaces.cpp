#include "interfaces.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#include <algorithm>

#pragma comment(lib, "iphlpapi.lib")

namespace t5000::net
{
    namespace
    {
        std::string wide_to_utf8(const wchar_t* w)
        {
            if (w == nullptr || *w == L'\0')
                return std::string();

            const int need = ::WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
            if (need <= 1)
                return std::string();

            std::string out((size_t)need - 1, '\0');
            ::WideCharToMultiByte(CP_UTF8, 0, w, -1, &out[0], need, nullptr, nullptr);
            return out;
        }

        bool contains_nocase(const std::string& haystack, const char* needle)
        {
            const std::string n(needle);
            if (n.empty() || haystack.size() < n.size())
                return false;

            auto lower = [](unsigned char c) { return (char)::tolower(c); };
            for (size_t i = 0; i + n.size() <= haystack.size(); i++)
            {
                size_t j = 0;
                while (j < n.size() && lower((unsigned char)haystack[i + j]) ==
                                       lower((unsigned char)n[j]))
                    j++;
                if (j == n.size())
                    return true;
            }
            return false;
        }

        // Named by what they are rather than by vendor where possible, because
        // this list ages badly and a wrong guess only changes the sort order.
        bool looks_like_a_virtual_adapter(const std::string& text)
        {
            static const char* kHints[] = {
                "hyper-v", "vethernet", "wsl", "virtualbox", "vmware",
                "vpn", "tap-", "tunnel", "loopback adapter", "docker",
                "bluetooth", "wintun", "zerotier", "tailscale",
            };
            for (const char* h : kHints)
                if (contains_nocase(text, h))
                    return true;
            return false;
        }
    }

    std::vector<Interface> ipv4_interfaces(std::string& error)
    {
        error.clear();
        std::vector<Interface> found;

        // GetAdaptersAddresses wants a buffer it can tell us is too small, and
        // the size can change between the two calls on a machine where an
        // adapter appears mid-query. Retry a bounded number of times rather
        // than looping on a condition the OS controls.
        ULONG size = 16 * 1024;
        std::vector<unsigned char> buffer;
        ULONG result = ERROR_BUFFER_OVERFLOW;

        const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                            GAA_FLAG_SKIP_DNS_SERVER;

        for (int attempt = 0; attempt < 4 && result == ERROR_BUFFER_OVERFLOW; attempt++)
        {
            buffer.assign(size, 0);
            result = ::GetAdaptersAddresses(AF_INET, flags, nullptr,
                                            (IP_ADAPTER_ADDRESSES*)buffer.data(), &size);
        }

        if (result != NO_ERROR)
        {
            error = "could not list network interfaces (error " +
                    std::to_string((unsigned long)result) + ")";
            return found;
        }

        for (auto* a = (IP_ADAPTER_ADDRESSES*)buffer.data(); a != nullptr; a = a->Next)
        {
            const std::string name = wide_to_utf8(a->FriendlyName);
            const std::string desc = wide_to_utf8(a->Description);

            for (auto* u = a->FirstUnicastAddress; u != nullptr; u = u->Next)
            {
                if (u->Address.lpSockaddr == nullptr ||
                    u->Address.lpSockaddr->sa_family != AF_INET)
                    continue;

                const auto* in4 = (const sockaddr_in*)u->Address.lpSockaddr;

                char text[INET_ADDRSTRLEN] = {};
                if (::inet_ntop(AF_INET, &in4->sin_addr, text, sizeof(text)) == nullptr)
                    continue;

                Interface iface;
                iface.ip          = text;
                iface.name        = name.empty() ? std::string(text) : name;
                iface.description = desc;
                iface.is_up       = (a->OperStatus == IfOperStatusUp);
                iface.is_loopback = (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK);
                iface.looks_virtual =
                    !iface.is_loopback &&
                    (looks_like_a_virtual_adapter(name) || looks_like_a_virtual_adapter(desc));

                found.push_back(iface);
            }
        }

        // Best candidate first: a real adapter that is up beats a virtual one,
        // which beats anything down, which beats loopback. std::stable_sort so
        // two equally plausible adapters keep the order Windows gave them,
        // which is the order the operator sees elsewhere in the OS.
        std::stable_sort(found.begin(), found.end(),
                         [](const Interface& x, const Interface& y) {
                             auto rank = [](const Interface& i) {
                                 if (i.is_loopback)    return 3;
                                 if (!i.is_up)         return 2;
                                 if (i.looks_virtual)  return 1;
                                 return 0;
                             };
                             return rank(x) < rank(y);
                         });

        return found;
    }
}
