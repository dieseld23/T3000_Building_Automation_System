#include "connection.h"

#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace t3000::device
{
    const char* transport_name(Transport t)
    {
        switch (t)
        {
        case Transport::BacnetIp:   return "bacnet-ip";
        case Transport::BacnetMstp: return "bacnet-mstp";
        case Transport::ModbusTcp:  return "modbus-tcp";
        case Transport::ModbusRtu:  return "modbus-rtu";
        }
        return "bacnet-ip";
    }

    bool transport_from_name(const std::string& name, Transport& out)
    {
        if (name == "bacnet-ip")   { out = Transport::BacnetIp;   return true; }
        if (name == "bacnet-mstp") { out = Transport::BacnetMstp; return true; }
        if (name == "modbus-tcp")  { out = Transport::ModbusTcp;  return true; }
        if (name == "modbus-rtu")  { out = Transport::ModbusRtu;  return true; }
        return false;
    }

    bool transport_is_serial(Transport t)
    {
        return t == Transport::BacnetMstp || t == Transport::ModbusRtu;
    }

    const std::vector<int>& supported_baud_rates()
    {
        static const std::vector<int> rates = { 9600, 19200, 38400, 57600, 76800, 115200 };
        return rates;
    }

    namespace
    {
        bool looks_like_host(const std::string& h)
        {
            if (h.empty())
                return false;

            // Not a hostname validator. It rejects the things people actually
            // paste in by mistake - a URL, a host:port pair, whitespace - and
            // leaves genuine resolution to the connect attempt, which is the
            // only thing that can really answer it.
            if (h.find("://") != std::string::npos) return false;
            if (h.find(':')   != std::string::npos) return false;
            if (h.find(' ')   != std::string::npos) return false;
            if (h.find('/')   != std::string::npos) return false;
            return true;
        }
    }

    std::vector<ValidationError> validate(const Connection& c)
    {
        std::vector<ValidationError> errors;

        if (transport_is_serial(c.transport))
        {
            if (c.com_port < 1 || c.com_port > 255)
                errors.push_back({ "comPort", "COM port must be between 1 and 255." });

            const auto& rates = supported_baud_rates();
            bool known = false;
            for (int r : rates)
                if (r == c.baud) known = true;

            if (!known)
            {
                std::string msg = "Baud rate must be one of";
                for (size_t i = 0; i < rates.size(); i++)
                    msg += (i ? ", " : " ") + std::to_string(rates[i]);
                msg += ". The controllers do not offer others.";
                errors.push_back({ "baud", msg });
            }
        }
        else
        {
            if (!looks_like_host(c.host))
            {
                errors.push_back({ "host",
                    "Enter the controller's address as a bare host or IP - no scheme, "
                    "port or path. Use the port field for the port." });
            }
        }

        if (c.transport == Transport::BacnetIp)
        {
            if (c.udp_port < 1 || c.udp_port > 65535)
                errors.push_back({ "udpPort", "UDP port must be between 1 and 65535." });

            // Worth saying rather than silently allowing: the product documents
            // 47808 as fixed, so a different value is usually a mistake.
            if (c.udp_port != 47808)
            {
                errors.push_back({ "udpPort",
                    "BACnet/IP normally uses 47808 and T3000 treats it as fixed. "
                    "Change this only if the site genuinely runs a different port." });
            }
        }

        if (c.transport == Transport::ModbusTcp)
        {
            if (c.tcp_port < 1 || c.tcp_port > 65535)
                errors.push_back({ "tcpPort", "TCP port must be between 1 and 65535." });
        }

        if (c.transport == Transport::BacnetMstp)
        {
            if (c.mstp_max_master < 1 || c.mstp_max_master > 127)
                errors.push_back({ "mstpMaxMaster", "Max master must be between 1 and 127." });
        }

        if (c.transport == Transport::ModbusTcp || c.transport == Transport::ModbusRtu)
        {
            if (c.modbus_slave_id < 1 || c.modbus_slave_id > 247)
                errors.push_back({ "modbusSlaveId", "Modbus slave id must be between 1 and 247." });
        }

        if (c.device_instance < 0 || c.device_instance > 4194302)
            errors.push_back({ "deviceInstance", "BACnet device instance must be between 0 and 4194302." });

        return errors;
    }

    std::string to_json(const Connection& c)
    {
        char buf[768];
        snprintf(buf, sizeof(buf),
            "{\"transport\":\"%s\","
            "\"host\":\"%s\","
            "\"udpPort\":%d,"
            "\"tcpPort\":%d,"
            "\"comPort\":%d,"
            "\"baud\":%d,"
            "\"deviceInstance\":%d,"
            "\"mstpMaxMaster\":%d,"
            "\"modbusSlaveId\":%d}",
            transport_name(c.transport),
            c.host.c_str(),
            c.udp_port, c.tcp_port, c.com_port, c.baud,
            c.device_instance, c.mstp_max_master, c.modbus_slave_id);
        return buf;
    }

    namespace
    {
        // A flat-object reader, not a JSON parser. The config is one level deep
        // with string and integer values, and a real parser would be the largest
        // dependency in the project for no gain. It does not accept nesting,
        // arrays or escapes, and says so rather than guessing.
        bool find_value(const std::string& json, const std::string& key, std::string& out)
        {
            const std::string needle = "\"" + key + "\"";
            const size_t at = json.find(needle);
            if (at == std::string::npos)
                return false;

            size_t colon = json.find(':', at + needle.size());
            if (colon == std::string::npos)
                return false;

            size_t i = colon + 1;
            while (i < json.size() && isspace((unsigned char)json[i])) i++;
            if (i >= json.size())
                return false;

            if (json[i] == '"')
            {
                const size_t start = ++i;
                while (i < json.size() && json[i] != '"') i++;
                if (i >= json.size())
                    return false;
                out = json.substr(start, i - start);
                return true;
            }

            const size_t start = i;
            while (i < json.size() && json[i] != ',' && json[i] != '}') i++;
            out = json.substr(start, i - start);

            while (!out.empty() && isspace((unsigned char)out.back()))
                out.pop_back();
            return !out.empty();
        }

        void read_int(const std::string& json, const char* key, int& target)
        {
            std::string raw;
            if (find_value(json, key, raw))
                target = (int)strtol(raw.c_str(), nullptr, 10);
        }
    }

    bool from_json(const std::string& json, Connection& out, std::string& error)
    {
        if (json.find('{') == std::string::npos)
        {
            error = "Settings must be a JSON object.";
            return false;
        }

        std::string transport;
        if (find_value(json, "transport", transport))
        {
            if (!transport_from_name(transport, out.transport))
            {
                error = "Unknown transport \"" + transport +
                        "\". Expected bacnet-ip, bacnet-mstp, modbus-tcp or modbus-rtu.";
                return false;
            }
        }

        find_value(json, "host", out.host);

        read_int(json, "udpPort",        out.udp_port);
        read_int(json, "tcpPort",        out.tcp_port);
        read_int(json, "comPort",        out.com_port);
        read_int(json, "baud",           out.baud);
        read_int(json, "deviceInstance", out.device_instance);
        read_int(json, "mstpMaxMaster",  out.mstp_max_master);
        read_int(json, "modbusSlaveId",  out.modbus_slave_id);

        return true;
    }

    std::string default_config_path()
    {
        char exe[MAX_PATH] = {};
        if (GetModuleFileNameA(nullptr, exe, MAX_PATH) == 0)
            return "T3000Config.connection.json";

        std::string path(exe);
        const size_t slash = path.find_last_of("\\/");
        if (slash != std::string::npos)
            path.resize(slash + 1);

        return path + "T3000Config.connection.json";
    }

    bool load(const std::string& path, Connection& out, std::string& error)
    {
        FILE* f = nullptr;
        if (fopen_s(&f, path.c_str(), "rb") != 0 || f == nullptr)
        {
            // Absent is not an error. A tool that has never been configured
            // should start on defaults and say so, not refuse to run.
            error.clear();
            return false;
        }

        std::string text;
        char buf[1024];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
            text.append(buf, n);
        fclose(f);

        return from_json(text, out, error);
    }

    bool save(const std::string& path, const Connection& c, std::string& error)
    {
        FILE* f = nullptr;
        if (fopen_s(&f, path.c_str(), "wb") != 0 || f == nullptr)
        {
            error = "Could not write " + path + ". Is the folder writable?";
            return false;
        }

        const std::string json = to_json(c);
        const bool ok = fwrite(json.data(), 1, json.size(), f) == json.size();
        fclose(f);

        if (!ok)
            error = "Could not finish writing " + path + ".";
        return ok;
    }
}
