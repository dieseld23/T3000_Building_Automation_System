#include "find_device.h"

#include <map>

#include "../display/device_text.h"
#include "../json/read.h"
#include "../wire/panel.h"

namespace t5000::app
{
    namespace
    {
        using namespace t5000::device;

        void trim(std::string& s)
        {
            const char* const space = " \t\r\n";
            const size_t first = s.find_first_not_of(space);
            if (first == std::string::npos)
            {
                s.clear();
                return;
            }
            s.erase(s.find_last_not_of(space) + 1);
            s.erase(0, first);
        }

        // Four numbers from 0 to 255, each one to three digits, with no
        // leading zero, separated by dots, and nothing else. Stricter than
        // inet_pton on purpose: some parsers read "010" as octal 8, and an
        // address someone typed should mean one thing.
        bool dotted_quad(const std::string& s, uint32_t& ip)
        {
            ip = 0;
            size_t at = 0;
            for (int part = 0; part < 4; part++)
            {
                if (part > 0)
                {
                    if (at >= s.size() || s[at] != '.')
                        return false;
                    at++;
                }

                const size_t start = at;
                unsigned value = 0;
                while (at < s.size() && s[at] >= '0' && s[at] <= '9' && at - start < 3)
                    value = value * 10 + (unsigned)(s[at++] - '0');

                const size_t digits = at - start;
                if (digits == 0 || value > 255 || (digits > 1 && s[start] == '0'))
                    return false;
                ip = (ip << 8) | value;
            }
            return at == s.size();
        }

        uint32_t mask_of(int prefix_length)
        {
            return prefix_length <= 0 ? 0u : prefix_length >= 32 ? 0xFFFFFFFFu : 0xFFFFFFFFu << (32 - prefix_length);
        }

        std::string dotted(uint32_t ip)
        {
            bacnet::Endpoint e;
            e.ip = ip;
            const std::string text = e.text();
            return text.substr(0, text.find(':'));
        }

        // Why `ip` is the network or broadcast address of one of this
        // computer's networks, or empty when it is neither. A /31 or /32
        // has neither, and a prefix Windows did not give is not guessed at.
        std::string local_network_address(uint32_t ip, const std::vector<net::Interface>& local)
        {
            for (const auto& i : local)
            {
                uint32_t own = 0;
                if (i.prefix_length < 1 || i.prefix_length > 30 || !dotted_quad(i.ip, own))
                    continue;

                const uint32_t mask    = mask_of(i.prefix_length);
                const uint32_t network = own & mask;
                const std::string subnet = dotted(network) + "/" + std::to_string(i.prefix_length);
                const std::string on     = ", the network of this computer's " + i.name;

                if (ip == (network | ~mask))
                    return dotted(ip) + " is the broadcast address of " + subnet + on +
                           ", which every device on it receives. Find asks one device, at its own address.";
                if (ip == network)
                    return dotted(ip) + " is the address of the network " + subnet + on +
                           " itself, not of a device on it.";
            }
            return std::string();
        }

        // The address without the port, as the list shows it.
        std::string host_text(const bacnet::Endpoint& at)
        {
            const std::string text = at.text();
            return text.substr(0, text.find(':'));
        }

        std::string seconds_text(int ms)
        {
            return std::to_string(ms / 1000.0).substr(0, 3);
        }

        // What the settings call the panel: the bytes before the first NUL,
        // from the ANSI code page, with trailing blanks dropped as the scan
        // drops them (discovery/scan_response.cpp).
        std::string panel_name_of(const wire::PanelSettings& s)
        {
            std::string name = display::acp_to_utf8(s.panel_name, wire::settings_at::panel_name_length);
            while (!name.empty() && (name.back() == ' ' || name.back() == '\t'))
                name.pop_back();
            return name;
        }

        // The entry another serial belongs to, for a message, or empty when
        // it is not in the list.
        std::string listed_as(const Registry& registry, uint32_t serial)
        {
            for (const auto& d : registry.devices())
            {
                if (d.serial_number != serial || !d.has_stable_identity())
                    continue;
                const std::string& name = !d.placement.name.empty() ? d.placement.name : d.panel_name;
                return " A device with serial " + std::to_string(serial) + " is in the list already" +
                       (name.empty() ? std::string() : ", named \"" + name + "\"") + ".";
            }
            return std::string();
        }

        const char* const kNothingSaved = " Nothing was saved.";
    }

    bool read_find_request(const std::string& body, FindRequest& request, std::string& message)
    {
        std::map<std::string, json::FlatValue> fields;
        std::string error;
        if (!json::parse_flat_object(body, fields, error))
        {
            message = "The request could not be read: " + error + ".";
            return false;
        }

        FindRequest r;

        const auto handle = fields.find("handle");
        unsigned long long raw = 0;
        if (handle == fields.end() || !json::parse_u64(handle->second.text, raw) || raw == 0)
        {
            message = "handle must be a device's handle, a whole number above 0.";
            return false;
        }
        r.handle = to_handle(raw);

        const auto host = fields.find("host");
        if (host == fields.end() || !host->second.is_string)
        {
            message = "host must be a string: the address to look at.";
            return false;
        }
        r.host = host->second.text;

        // Left out or empty: BACnet/IP's own port, as the dialog offers.
        const auto port = fields.find("port");
        if (port != fields.end())
        {
            std::string text = port->second.text;
            trim(text);
            if (!text.empty())
            {
                unsigned long long n = 0;
                if (!json::parse_u64(text, n) || n == 0 || n > 65535)
                {
                    message = "The port must be a whole number from 1 to 65535. BACnet/IP devices use 47808.";
                    return false;
                }
                r.port = (int)n;
            }
        }

        request = r;
        return true;
    }

    std::string why_not_findable(const DeviceRecord& d)
    {
        // First: whatever else is true of it, there is no device to find.
        if (d.is_virtual())
            return "It is a virtual device: a configuration with no device behind it, so there is none to find. "
                   "Its serial is one T5000 gave it, not a device's.";

        if (!d.has_stable_identity())
            return "It has no serial number, so there is nothing to match a device at an address against.";

        // Its address is the one it answered this session's scan from, and
        // the scan vouches for it (Identity::VouchedForByScan). A Find
        // would put an address the operator typed under that vouching.
        if (d.answered_scan != 0)
            return "It has answered a scan since T5000 started, so T5000 already has its address. If it has "
                   "moved, scan again.";

        // Its scan answer came from the controller whose bus it is on, and
        // any address given for it would be that controller's, whose
        // settings give the controller's serial. Refused before anything is
        // sent, as plan_points_read refuses to read it.
        if (d.parent_serial != 0)
            return "It is on the RS485 bus of controller " + std::to_string(d.parent_serial) +
                   ", which answers for it, and T5000 does not reach a device through its controller yet.";

        const Capabilities& cap = capabilities(d.product);
        if (cap.path != DataPath::BacnetPrivateData)
            return std::string("Find reads a panel's settings by BACnet private transfer, which only the CM5, "
                               "MiniPanel, MiniPanel ARM, T3 Series (ESP32) and TSTAT10 answer. The ") +
                   cap.name + " is not one of them yet.";

        return std::string();
    }

    bool find_endpoint(const std::string& host, int port, const std::vector<net::Interface>& local,
                       bacnet::Endpoint& at, std::string& message)
    {
        std::string h = host;
        trim(h);
        if (h.empty())
        {
            message = "Enter the address to look at: an IPv4 address, such as 192.168.1.50.";
            return false;
        }

        uint32_t ip = 0;
        if (!dotted_quad(h, ip))
        {
            message = "'" + h + "' is not an IPv4 address. Enter four numbers from 0 to 255 separated by dots, "
                      "such as 192.168.1.50. T5000 does not look up names.";
            return false;
        }
        if (port < 1 || port > 65535)
        {
            message = "The port must be a whole number from 1 to 65535. BACnet/IP devices use 47808.";
            return false;
        }

        // Addresses that are never one device. The same ones parse_endpoint
        // refuses, and the reserved range too, each said in words.
        if (ip == 0)
        {
            message = "0.0.0.0 is not a device's address.";
            return false;
        }
        if (ip == 0xFFFFFFFFu)
        {
            message = "255.255.255.255 is the broadcast address, which every device on the network receives. "
                      "Find asks one device, at its own address.";
            return false;
        }
        if ((ip >> 28) == 0xE)
        {
            message = h + " is a multicast address, which is not one device's.";
            return false;
        }
        if ((ip >> 28) == 0xF)
        {
            message = h + " is in the reserved range from 240.0.0.0, which no device is given.";
            return false;
        }

        const std::string network = local_network_address(ip, local);
        if (!network.empty())
        {
            message = network;
            return false;
        }

        bacnet::Endpoint e;
        if (!bacnet::parse_endpoint(h, port, e))
        {
            message = "'" + h + "' port " + std::to_string(port) + " is not an address T5000 can send to.";
            return false;
        }
        at = e;
        return true;
    }

    bool plan_find(const Registry& registry, const FindRequest& request, const std::vector<net::Interface>& local,
                   DeviceRecord& device, bacnet::Endpoint& at, std::string& message)
    {
        const DeviceRecord* d = nullptr;
        for (const auto& candidate : registry.devices())
            if (candidate.handle == request.handle)
                d = &candidate;
        if (!d || request.handle == kNoHandle)
        {
            message = "That device is no longer in the list. The page may be out of date. Nothing was sent.";
            return false;
        }

        const std::string why = why_not_findable(*d);
        if (!why.empty())
        {
            message = why + " Nothing was sent.";
            return false;
        }

        std::string refused;
        if (!find_endpoint(request.host, request.port, local, at, refused))
        {
            message = refused + " Nothing was sent.";
            return false;
        }

        device = *d;
        return true;
    }

    FindOutcome find_at(bacnet::ReadTransport& transport, const bacnet::Endpoint& at, const DeviceRecord& device,
                        const Registry& registry, const bacnet::ReadSettings& settings, uint8_t& next_invoke_id,
                        int64_t now)
    {
        using namespace bacnet;

        FindOutcome out;
        const std::string where    = at.text();
        const std::string expected = std::to_string(device.serial_number);

        const ReadOutcome s = read_entities(transport, at, ReadCommand::Settings, 1, 1,
                                            (uint16_t)wire::kSettingsWireSize, settings, next_invoke_id);
        out.requests_sent = s.requests_sent;

        // Each of these stands alone in the Find dialog, and follows "Found."
        // or "Not found." in the page's banner.
        if (!s.ok)
        {
            // read_entities' own words for silence, and for a port nothing
            // listens on, are about a device a scan found; this one may never
            // have been reached.
            if (s.no_answer)
                out.message = "Nothing answered at " + where + " after " + std::to_string(settings.attempts) +
                              (settings.attempts == 1 ? " attempt" : " attempts") + " of " +
                              seconds_text(settings.reply_timeout_ms) +
                              " s. Check the address and the port, that this computer can reach that network, and "
                              "that no firewall blocks UDP " + std::to_string(at.port) + ".";
            else if (s.port_unreachable)
                out.message = host_text(at) + " answered that nothing is listening on UDP port " +
                              std::to_string(at.port) + ", so something is at that address, but not on that port. "
                              "Check the port: BACnet/IP devices use 47808.";
            else
                out.message = s.error;
            out.message += kNothingSaved;
            return out;
        }

        wire::PanelSettings panel;
        if (!wire::decode_settings(s.entities.data(), s.entities.size(), panel))
        {
            out.message = "The panel at " + where + " answered, but its settings could not be decoded, so there is "
                          "no serial to check." + kNothingSaved;
            return out;
        }

        const uint32_t reported = panel.serial_number;
        if (reported == 0)
        {
            out.message = "The panel at " + where + " gives no serial number in its settings, so T5000 cannot tell "
                          "whether it is serial " + expected + "." + kNothingSaved;
            return out;
        }
        if (reported != device.serial_number)
        {
            out.message = "The panel at " + where + " gives its serial number as " +
                          std::to_string(reported) + " in its settings, not " + expected +
                          ", so it is another device." + listed_as(registry, reported) + kNothingSaved;
            return out;
        }

        // The device as this one answer shows it. Only what the settings
        // give, and the address they came from; the product stays the
        // entry's, and the firmware is left for a scan (find_device.h).
        DeviceRecord f;
        f.handle        = device.handle;   // which entry it is for; see record_found
        f.serial_number = device.serial_number;
        f.product       = device.product;
        f.mini_type     = panel.mini_type();
        f.provenance    = Provenance::BacnetUnicast;
        f.reached       = true;
        f.first_seen    = now;
        f.last_seen     = now;

        f.connection.transport       = Transport::BacnetIp;
        f.connection.host            = host_text(at);
        f.connection.udp_port        = at.port;
        f.connection.device_instance = (int)panel.object_instance;
        f.connection.modbus_slave_id = panel.modbus_id;
        f.modbus_id_reported         = panel.modbus_id;
        f.address_note               = f.connection.host;

        // The pair a scan answer gives, from this answer: it came from the
        // address asked, and it says nothing about the address the device
        // reports for itself. Registry::add_or_merge takes the pair from a
        // Find, so an older scan's pair does not go on describing an
        // address T5000 no longer contacts.
        f.answered_from = f.connection.host;
        f.reported_ip.clear();

        f.panel_name = panel_name_of(panel);

        out.found   = true;
        out.record  = f;
        out.message = "The panel at " + where + " gives serial " + expected +
                      " in its settings. Its pages are read from that address now.";
        return out;
    }

    bool record_found(Registry& registry, store::DeviceDb& db, const DeviceRecord& found, ScanSummary& summary,
                      std::string& message)
    {
        Registry next = registry;
        const int i = next.add_or_merge(found);

        // Merged into the entry it was found for, or not at all. A record
        // that carries a handle and lands anywhere else - added as a new
        // device, because that entry has gone - is not recorded.
        if (found.handle != kNoHandle && next.devices()[(size_t)i].handle != found.handle)
        {
            message = "That device is no longer in the list, so what was found was not recorded.";
            return false;
        }

        const int duplicates = next.refresh_duplicate_modbus_ids();

        if (db.is_open())
        {
            // The merged record, not the found one: a field the Find did not
            // carry keeps what the list had, on disk as in memory.
            std::string error;
            if (!db.save_scanned({ next.devices()[(size_t)i] }, error))
            {
                message = "It was found, but the list could not be saved, so nothing was changed: " + error;
                return false;
            }
        }
        else
        {
            message += " The list is not being saved, so the address is kept only until T5000 closes.";
        }

        registry = next;
        summary.stats.duplicate_modbus_ids = duplicates;
        return true;
    }

    bool find_device(Registry& registry, store::DeviceDb& db, const DeviceRecord& device, const bacnet::Endpoint& at,
                     bacnet::ReadTransport& transport, const bacnet::ReadSettings& settings, uint8_t& next_invoke_id,
                     int64_t now, ScanSummary& summary, std::string& message)
    {
        const FindOutcome outcome = find_at(transport, at, device, registry, settings, next_invoke_id, now);
        message = outcome.message;
        if (!outcome.found)
            return false;
        return record_found(registry, db, outcome.record, summary, message);
    }
}
