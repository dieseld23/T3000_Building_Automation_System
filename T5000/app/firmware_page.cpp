#include "firmware_page.h"

#include "points_json.h"
#include "../json/read.h"

namespace t5000::app
{
    namespace
    {
        using device::DeviceRecord;
        using device::Provenance;

        void key(std::string& out, const char* name)
        {
            out += '"';
            out += name;
            out += "\":";
        }

        void text(std::string& out, const char* name, const std::string& value)
        {
            key(out, name);
            out += '"' + json_escape(value) + '"';
        }

        void number(std::string& out, const char* name, long long value)
        {
            key(out, name);
            out += std::to_string(value);
        }

        void flag(std::string& out, const char* name, bool value)
        {
            key(out, name);
            out += value ? "true" : "false";
        }

        void sentences(std::string& out, const char* name, const std::vector<std::string>& list)
        {
            key(out, name);
            out += '[';
            for (size_t i = 0; i < list.size(); i++)
            {
                if (i != 0)
                    out += ',';
                out += '"' + json_escape(list[i]) + '"';
            }
            out += ']';
        }

        int hex_digit(char c)
        {
            if (c >= '0' && c <= '9')
                return c - '0';
            if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;
            if (c >= 'A' && c <= 'F')
                return c - 'A' + 10;
            return -1;
        }

        const DeviceRecord* find(const device::Registry& registry, device::Handle handle)
        {
            for (const auto& d : registry.devices())
                if (d.handle == handle)
                    return &d;
            return nullptr;
        }

        const char* format_text(firmware::HexFormat format)
        {
            switch (format)
            {
            case firmware::HexFormat::Data:           return "data records";
            case firmware::HexFormat::SegmentAddress: return "segment address records";
            case firmware::HexFormat::LinearAddress:  return "linear address records";
            }
            return "";
        }

        const char* chip_key(firmware::Chip chip)
        {
            switch (chip)
            {
            case firmware::Chip::Asix:   return "asix";
            case firmware::Chip::Arm32K: return "arm32k";
            case firmware::Chip::Arm64K: return "arm64k";
            }
            return "";
        }

        const char* kNothingSent = " Nothing was sent.";
    }

    bool firmware_path(const DeviceRecord& d, firmware::Path& path)
    {
        if (d.is_virtual())
            return false;

        // Subnote: a device on a controller's RS485 bus is flashed through
        // the controller, whatever it is reached over. Then COM_OR_NET.
        if (d.parent_serial != 0)
            path = firmware::Path::Controller;
        else if (device::transport_is_serial(d.connection.transport))
            path = firmware::Path::Serial;
        else
            path = firmware::Path::Network;
        return true;
    }

    const char* path_key(firmware::Path path)
    {
        switch (path)
        {
        case firmware::Path::Serial:     return "serial";
        case firmware::Path::Network:    return "network";
        case firmware::Path::Controller: return "controller";
        }
        return "";
    }

    std::string path_text(firmware::Path path)
    {
        switch (path)
        {
        case firmware::Path::Serial:
            return "On its serial port: ISP sends a file by Modbus RTU.";
        case firmware::Path::Network:
            return "On the network: ISP sends a file to the device's bootloader by TFTP.";
        case firmware::Path::Controller:
            return "Through the controller whose RS485 bus it is on: ISP sends a file by Modbus TCP.";
        }
        return "";
    }

    firmware::DeviceFacts device_facts(const DeviceRecord& d)
    {
        firmware::DeviceFacts f;
        f.product          = (int)static_cast<uint8_t>(d.product);
        f.bootloader_known = d.bootloader_known;
        f.bootloader       = d.bootloader;
        f.bootloader_from  = d.bootloader_from;
        return f;
    }

    bool reached_this_session(const DeviceRecord& d)
    {
        // answered_scan is set only by a scan in this session. A device Find
        // found this session is BacnetUnicast and has not answered one: the
        // saved list restores every device as Restored (app/inputs_plan.cpp).
        return d.answered_scan != 0 || d.provenance == Provenance::BacnetUnicast;
    }

    std::vector<std::string> own_refusals(const DeviceRecord& d)
    {
        std::vector<std::string> out;
        if (d.is_virtual())
        {
            out.push_back("Serial " + std::to_string(d.serial_number) +
                          " is a virtual device: a configuration with no device behind it. It is never flashed.");
            return out;
        }
        if (!reached_this_session(d))
        {
            if (d.provenance == Provenance::ManuallyAdded)
                out.push_back("This entry was added by hand, and no device with its serial has been found. T5000 "
                              "flashes a device only once a scan, or Find, has reached it this session.");
            else
                out.push_back("It has not answered a scan, or Find, since T5000 started. T5000 flashes a device "
                              "only once one has reached it this session, so that its address is the device's now.");
        }
        return out;
    }

    const char* bootloader_state_key(const DeviceRecord& d)
    {
        if (!d.bootloader_state_known)
            return "unknown";
        return d.in_bootloader ? "bootloader" : "firmware";
    }

    std::string bootloader_state_text(const DeviceRecord& d)
    {
        if (d.is_virtual())
            return "A virtual device has no bootloader.";
        if (!d.bootloader_state_known)
            return device::transport_is_serial(d.connection.transport) && d.answered_scan != 0
                       ? "A serial scan does not say whether a device is in its bootloader."
                       : "It has not answered a network scan since T5000 started, which would say.";
        if (d.in_bootloader)
            return "Its last scan response said it is in its bootloader: it answers a scan, and ISP, but not reads.";
        return "Its last scan response said its firmware is running.";
    }

    std::string firmware_device_json(const DeviceRecord& d, bool can_read, const std::string& read_why)
    {
        const device::Capabilities& cap = device::capabilities(d.product);

        std::string out = "{";
        text(out, "handle", std::to_string(device::to_number(d.handle)));     out += ',';
        number(out, "serialNumber", (long long)d.serial_number);             out += ',';
        text(out, "name", d.placement.name);                                  out += ',';
        text(out, "panelName", d.panel_name);                                 out += ',';
        number(out, "productId", (long long)static_cast<uint8_t>(d.product)); out += ',';
        text(out, "productName", std::string(cap.name));                      out += ',';
        number(out, "firmware", (long long)d.firmware);                       out += ',';
        text(out, "transport", device::transport_name(d.connection.transport)); out += ',';
        text(out, "address", d.address_note);                                 out += ',';
        flag(out, "virtual", d.is_virtual());                                 out += ',';
        flag(out, "reached", reached_this_session(d));                       out += ',';

        firmware::Path path = firmware::Path::Network;
        const bool has_path = firmware_path(d, path);
        text(out, "path", has_path ? path_key(path) : "");                    out += ',';
        text(out, "pathText", has_path ? path_text(path) : std::string());    out += ',';

        text(out, "state", bootloader_state_key(d));                          out += ',';
        text(out, "stateText", bootloader_state_text(d));                     out += ',';

        key(out, "bootloader");
        out += '{';
        flag(out, "known", d.bootloader_known);                               out += ',';
        number(out, "version", d.bootloader_known ? d.bootloader : 0);        out += ',';
        text(out, "from", d.bootloader_known ? d.bootloader_from : std::string());
        out += "},";

        flag(out, "canRead", can_read);                                       out += ',';
        text(out, "readWhy", can_read ? std::string() : read_why);           out += ',';
        sentences(out, "own", own_refusals(d));
        out += '}';
        return out;
    }

    bool percent_decode(const std::string& text, std::string& out)
    {
        out.clear();
        for (size_t i = 0; i < text.size(); i++)
        {
            if (text[i] != '%')
            {
                out += text[i];
                continue;
            }
            if (i + 2 >= text.size())
                return false;
            const int hi = hex_digit(text[i + 1]);
            const int lo = hex_digit(text[i + 2]);
            if (hi < 0 || lo < 0)
                return false;
            out += (char)(hi * 16 + lo);
            i += 2;
        }
        return true;
    }

    bool read_firmware_check_request(const std::string& query, FirmwareCheckRequest& request, std::string& message)
    {
        const std::string form = "The address must end ?handle=<the device's handle>&name=<the file's name>.";

        FirmwareCheckRequest r;
        bool have_handle = false;
        bool have_name   = false;

        size_t at = 0;
        while (at <= query.size())
        {
            size_t end = query.find('&', at);
            if (end == std::string::npos)
                end = query.size();
            const std::string part = query.substr(at, end - at);
            at = end + 1;

            const size_t eq = part.find('=');
            if (eq == std::string::npos)
            {
                message = form;
                return false;
            }
            const std::string name  = part.substr(0, eq);
            const std::string value = part.substr(eq + 1);

            if (name == "handle")
            {
                unsigned long long raw = 0;
                if (have_handle || !json::parse_u64(value, raw) || raw == 0)
                {
                    message = "handle must be a device's handle, once, as the list gives it.";
                    return false;
                }
                r.handle    = device::to_handle(raw);
                have_handle = true;
            }
            else if (name == "name")
            {
                if (have_name || !percent_decode(value, r.name))
                {
                    message = "name must be the file's name, once, encoded as encodeURIComponent encodes it.";
                    return false;
                }
                have_name = true;
            }
            else
            {
                message = form;
                return false;
            }
        }

        if (!have_handle || !have_name)
        {
            message = form;
            return false;
        }
        if (r.name.empty() || r.name.size() > 255)
        {
            message = "The file's name must be 1 to 255 bytes long.";
            return false;
        }
        for (const char c : r.name)
        {
            if ((unsigned char)c < 0x20 || c == 0x7F)
            {
                message = "The file's name has a control character in it.";
                return false;
            }
        }

        request = r;
        return true;
    }

    std::string check_firmware_json(const device::Registry& registry, const FirmwareCheckRequest& request,
                                    const std::string& file)
    {
        const DeviceRecord* d = find(registry, request.handle);
        if (!d)
            return "{\"ok\":false,\"message\":\"That device is no longer in the list." + std::string(kNothingSent) +
                   "\"}";

        const std::vector<std::string> own = own_refusals(*d);

        std::string out = "{";
        firmware::Path path = firmware::Path::Network;
        if (!firmware_path(*d, path))
        {
            flag(out, "ok", false);                                                                out += ',';
            text(out, "message", "A virtual device is never flashed, so the file was not read." +
                                     std::string(kNothingSent));                                   out += ',';
            sentences(out, "own", own);
            out += '}';
            return out;
        }

        firmware::FirmwareFile f;
        std::string why;
        const bool read = firmware::read_firmware(request.name, reinterpret_cast<const uint8_t*>(file.data()),
                                                  file.size(), path, f, why);

        firmware::Verdict verdict;
        if (read)
            verdict = firmware::check_firmware(f, device_facts(*d));
        const bool ok = read && verdict.ok && own.empty();

        std::string message;
        if (!read)
            message = why + kNothingSent;
        else if (ok)
            message = "ISP would take this file for this device, and so would T5000. Nothing was sent: T5000 does "
                      "not flash firmware yet.";
        else if (verdict.ok)
            message = "ISP would take this file for this device, but T5000 would not send it yet, for the reasons "
                      "below." + std::string(kNothingSent);
        else
            message = "T5000 would not send this file to this device, for the reasons below." +
                      std::string(kNothingSent);

        flag(out, "ok", ok);                           out += ',';
        text(out, "message", message);                 out += ',';
        text(out, "path", path_key(path));             out += ',';
        text(out, "pathText", path_text(path));        out += ',';

        key(out, "file");
        out += '{';
        text(out, "name", request.name);               out += ',';
        number(out, "size", (long long)file.size());   out += ',';
        flag(out, "read", read);
        if (read)
        {
            bool ends = true;
            const std::string product = f.header.product_name(ends);
            out += ',';
            text(out, "kind", f.kind == firmware::FileKind::Bin ? "bin" : "hex");                     out += ',';
            text(out, "format", f.kind == firmware::FileKind::Bin ? "" : format_text(f.format));     out += ',';
            text(out, "chip", f.format == firmware::HexFormat::LinearAddress && f.kind == firmware::FileKind::Hex
                                  ? chip_key(f.chip) : "");                                           out += ',';
            number(out, "headerAt", (long long)f.header_at);                                          out += ',';
            text(out, "company", f.header.company());                                                 out += ',';
            text(out, "productName", ends ? product : std::string());                                 out += ',';
            number(out, "version", f.header.version());                                               out += ',';
            number(out, "dataSize", (long long)f.data_size);
        }
        out += "},";

        key(out, "verdict");
        if (read)
        {
            out += '{';
            flag(out, "ok", verdict.ok);                                        out += ',';
            text(out, "route", firmware::to_string(verdict.route));            out += ',';
            text(out, "deviceName", verdict.device_name);                      out += ',';
            text(out, "fileName", verdict.file_name);                          out += ',';
            number(out, "version", verdict.version);                           out += ',';
            flag(out, "needsNewBootloader", verdict.needs_new_bootloader);     out += ',';
            sentences(out, "refusals", verdict.refusals);                      out += ',';
            sentences(out, "notes", verdict.notes);
            out += '}';
        }
        else
        {
            out += "null";
        }
        out += ',';

        sentences(out, "own", own);
        out += '}';
        return out;
    }
}
