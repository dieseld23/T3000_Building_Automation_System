#include "scan_json.h"
#include "points_json.h"

namespace t5000::app
{
    namespace
    {
        using namespace t5000::device;

        void append_key(std::string& out, const char* key)
        {
            out += '"';
            out += key;
            out += "\":";
        }

        void append_field(std::string& out, const char* key, const std::string& text)
        {
            append_key(out, key);
            out += '"';
            out += json_escape(text);
            out += '"';
        }

        void append_field(std::string& out, const char* key, long long number)
        {
            append_key(out, key);
            out += std::to_string(number);
        }

        void append_field(std::string& out, const char* key, bool flag)
        {
            append_key(out, key);
            out += flag ? "true" : "false";
        }

        void append_repairs(std::string& out, const DeviceRecord& d)
        {
            out += "\"repairs\":[";
            for (size_t i = 0; i < d.repairs.size(); i++)
            {
                const Repair& r = d.repairs[i];
                if (i) out += ',';
                out += '{';
                append_field(out, "kind", std::string(to_string(r.kind)));  out += ',';
                append_field(out, "problem", r.problem);                    out += ',';
                append_field(out, "action", r.action);                      out += ',';
                append_field(out, "consequence", r.consequence);            out += ',';
                append_field(out, "reversible", r.reversible);              out += ',';
                append_field(out, "approved", r.approved);
                out += '}';
            }
            out += ']';
        }

        void append_device(std::string& out, const Registry& registry, const DeviceRecord& d,
                           bool selected)
        {
            const Capabilities& cap = capabilities(d.product);
            const PanelResolution panel = resolve_panel(d.product, d.mini_type);

            out += '{';

            // Handles are small integers, well inside what a JSON number
            // survives in JavaScript - and they are emitted as strings anyway.
            // A key that is only ever compared and echoed back gains nothing
            // from being a number, and would lose silently if one ever passed
            // 2^53 and started rounding.
            append_field(out, "handle", std::to_string(to_number(d.handle))); out += ',';
            append_field(out, "selected", selected);                          out += ',';

            append_field(out, "serialNumber", (long long)d.serial_number);    out += ',';
            append_field(out, "hasStableIdentity", d.has_stable_identity());  out += ',';

            append_field(out, "productId",
                         (long long)static_cast<uint8_t>(d.product));         out += ',';
            append_field(out, "productName", std::string(cap.name));          out += ',';
            append_field(out, "support", std::string(to_string(cap.support))); out += ',';

            // The panel type goes through the resolver rather than out raw,
            // because 0 means "a CM5" on one product and "not set" on every
            // other one, and guessing wrong invents point counts.
            append_key(out, "panel");
            out += '{';
            append_field(out, "raw", (long long)d.mini_type);                 out += ',';
            append_field(out, "resolved", panel.resolved);                    out += ',';
            append_field(out, "name", std::string(to_string(panel.type)));    out += ',';
            append_field(out, "reason", std::string(panel.reason));
            out += "},";

            append_field(out, "firmware", (long long)d.firmware);             out += ',';

            // What the device said its Modbus id is, with 0 meaning it did not
            // say. Not the connection's slave id, which defaults to 1 and would
            // report every silent device as sitting on id 1.
            append_field(out, "modbusId", (long long)d.modbus_id_reported); out += ',';
            append_field(out, "parentSerial", (long long)d.parent_serial);    out += ',';
            append_field(out, "address", d.address_note);                     out += ',';
            append_field(out, "answeredFrom", d.answered_from);               out += ',';
            append_field(out, "reportedIp", d.reported_ip);                   out += ',';
            append_field(out, "addressMismatch", d.address_mismatch());       out += ',';
            append_field(out, "provenance",
                         std::string(to_string(d.provenance)));               out += ',';
            append_field(out, "reached", d.reached);                          out += ',';

            // The panel's own name, and separately the operator's. The page
            // decides which to show; both go out so neither is lost.
            append_field(out, "panelName", d.panel_name);                     out += ',';
            append_key(out, "placement");
            out += '{';
            append_field(out, "name", d.placement.name);                      out += ',';
            append_field(out, "building", d.placement.building);              out += ',';
            append_field(out, "floor", d.placement.floor);                    out += ',';
            append_field(out, "room", d.placement.room);
            out += "},";

            // Unix seconds, 0 when not known. Whether it answered is decided
            // here, from the registry's scan count, rather than by the page
            // subtracting one total from another: the list now holds devices
            // from earlier sessions, and a count of responses says nothing
            // about which of them answered.
            append_field(out, "firstSeen", (long long)d.first_seen);          out += ',';
            append_field(out, "lastSeen", (long long)d.last_seen);            out += ',';
            append_field(out, "answeredLastScan", registry.answered_last_scan(d)); out += ',';
            append_field(out, "seenThisSession", d.answered_scan != 0);       out += ',';
            append_field(out, "needsAttention", d.needs_attention());         out += ',';
            append_repairs(out, d);
            out += '}';
        }

        void append_stats(std::string& out, const discovery::ScanStats& s)
        {
            out += '{';
            append_field(out, "datagramsReceived", (long long)s.datagrams_received);  out += ',';
            append_field(out, "responsesParsed",   (long long)s.responses_parsed);    out += ',';
            append_field(out, "ignored",           (long long)s.ignored);             out += ',';
            append_field(out, "malformed",         (long long)s.malformed);           out += ',';
            append_field(out, "inBootloader",      (long long)s.in_bootloader);       out += ',';
            append_field(out, "duplicateModbusIds",(long long)s.duplicate_modbus_ids); out += ',';
            append_field(out, "withoutSerial",     (long long)s.without_serial);
            out += '}';
        }
    }

    std::string build_devices_json(const Registry& registry, const ScanSummary& summary,
                                   const StoreStatus& store)
    {
        const Handle selected = registry.selected_handle();

        std::string out = "{\"devices\":[";
        for (int i = 0; i < registry.size(); i++)
        {
            if (i) out += ',';
            const DeviceRecord& d = registry.devices()[i];
            append_device(out, registry, d, selected != kNoHandle && d.handle == selected);
        }
        out += "],";

        append_field(out, "scanCount", (long long)registry.scan_count()); out += ',';

        append_key(out, "store");
        out += '{';
        append_field(out, "saving", store.saving);                   out += ',';
        append_field(out, "path", store.path);                       out += ',';
        append_field(out, "error", store.error);                     out += ',';
        append_field(out, "restored", (long long)store.restored);
        out += "},";

        // An empty string rather than 0: nothing selected is the absence of a
        // handle, and a page testing truthiness on it should not have to know
        // that 0 is the reserved one.
        append_field(out, "selectedHandle",
                     selected == kNoHandle ? std::string()
                                           : std::to_string(to_number(selected)));
        out += ',';

        append_field(out, "unidentifiedCount",
                     (long long)registry.unidentified_count());                out += ',';
        append_field(out, "pendingRepairs",
                     (long long)registry.pending_repairs().size());            out += ',';

        append_key(out, "scan");
        out += '{';
        append_field(out, "hasScanned", summary.has_scanned);          out += ',';
        append_field(out, "error", summary.error);                     out += ',';
        append_field(out, "interfaceIp", summary.interface_ip);        out += ',';
        append_field(out, "waitedMs", (long long)summary.waited_ms);   out += ',';
        append_key(out, "stats");
        append_stats(out, summary.stats);
        out += "},";

        // Stated by the server rather than assumed by the page. This is the
        // one claim in the tool a technician is being asked to trust with
        // their building, so it travels with the data instead of living only
        // in a hard-coded string on the page.
        append_field(out, "readOnly", true);
        out += '}';
        return out;
    }

    std::string build_device_json(const Registry& registry, Handle handle)
    {
        if (handle == kNoHandle)
            return "null";

        for (const auto& d : registry.devices())
        {
            if (d.handle != handle)
                continue;

            std::string out;
            append_device(out, registry, d, registry.selected_handle() == handle);
            return out;
        }
        return "null";
    }

    std::string build_interfaces_json(const std::vector<net::Interface>& interfaces,
                                      const std::string& error)
    {
        std::string out = "{\"interfaces\":[";
        for (size_t i = 0; i < interfaces.size(); i++)
        {
            const net::Interface& n = interfaces[i];
            if (i) out += ',';
            out += '{';
            append_field(out, "ip", n.ip);                    out += ',';
            append_field(out, "name", n.name);                out += ',';
            append_field(out, "description", n.description);  out += ',';
            append_field(out, "isUp", n.is_up);               out += ',';
            append_field(out, "isLoopback", n.is_loopback);   out += ',';
            append_field(out, "looksVirtual", n.looks_virtual);
            out += '}';
        }
        out += "],";
        append_field(out, "error", error);
        out += '}';
        return out;
    }
}
