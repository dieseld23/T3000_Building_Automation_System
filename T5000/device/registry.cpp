#include "registry.h"

#include <algorithm>
#include <map>

namespace t5000::device
{
    bool is_uninitialised_serial(unsigned int serial)
    {
        // 0xFFFFFFFFu, spelled as itself. T3000 writes 255*255*255*255 here,
        // which is a different number, so its all-FF branch is dead code.
        return serial == 0u || serial == 0xFFFFFFFFu;
    }

    bool DeviceRecord::needs_attention() const
    {
        for (const auto& r : repairs)
            if (!r.approved) return true;
        return false;
    }

    int Registry::add_or_merge(const DeviceRecord& device)
    {
        // A device with no serial cannot be identified, so it cannot be
        // merged with anything - including another device with no serial.
        // Two unidentified devices are two devices until proven otherwise,
        // and collapsing them would hide exactly the situation the operator
        // needs to see.
        if (device.has_stable_identity())
        {
            for (size_t i = 0; i < m_devices.size(); i++)
            {
                if (m_devices[i].serial_number != device.serial_number)
                    continue;

                DeviceRecord& existing = m_devices[i];

                // Merge conservatively: take newly-learned facts, keep what
                // we already had when the incoming record is silent. A
                // rescan that reaches a device over a different transport
                // should not blank the fields it did not happen to read.
                if (device.product != ProductClassId::Unknown) existing.product = device.product;
                if (device.mini_type != 0)                     existing.mini_type = device.mini_type;
                if (device.firmware != 0)                      existing.firmware = device.firmware;
                if (!device.address_note.empty())              existing.address_note = device.address_note;

                // A device that has been renumbered must carry its new id, or
                // the registry keeps comparing the old one and reports a
                // conflict that was resolved. Found by a test that moved a
                // device off a duplicate id and watched the warning survive.
                if (device.modbus_id_reported != 0)
                    existing.modbus_id_reported = device.modbus_id_reported;

                // Likewise reachability, field by field like everything else
                // here. This was a wholesale replace, which quietly broke the
                // rule stated three comments up.
                //
                // to_record leaves modbus_slave_id at 0 when the device
                // reported no id, and host is always set on a scan record - so
                // the replace always fired and overwrote a slave id learned
                // earlier with one the device never reported.
                if (!device.connection.host.empty())
                {
                    existing.connection.host      = device.connection.host;
                    existing.connection.transport = device.connection.transport;
                }
                if (device.connection.udp_port != 0)
                    existing.connection.udp_port = device.connection.udp_port;
                if (device.connection.tcp_port != 0)
                    existing.connection.tcp_port = device.connection.tcp_port;
                if (device.connection.modbus_slave_id != 0)
                    existing.connection.modbus_slave_id = device.connection.modbus_slave_id;
                if (device.connection.device_instance != 0)
                    existing.connection.device_instance = device.connection.device_instance;

                // Reached is sticky in the true direction only. Having once
                // talked to a device is a fact about the past; failing to
                // reach it now does not unmake it, and the UI shows liveness
                // separately.
                existing.reached = existing.reached || device.reached;

                // Provenance upgrades towards "actually answered". A device
                // typed in by hand that later answers a broadcast is no
                // longer merely claimed to exist.
                if (existing.provenance == Provenance::ManuallyAdded ||
                    existing.provenance == Provenance::Restored)
                {
                    if (device.provenance != Provenance::ManuallyAdded &&
                        device.provenance != Provenance::Restored)
                        existing.provenance = device.provenance;
                }

                // An approval NEVER survives a merge, whatever the incoming
                // record contains. Consent was given against what was known
                // at the time; re-approving costs a click, while silently
                // holding a write the operator agreed to under different
                // information does not have a bounded cost.
                //
                // This used to be inside the `if` below, which meant a
                // rescan that found NO problems left an old approved repair
                // sitting there - approved, and now describing a device
                // state that no longer exists.
                for (auto& r : existing.repairs)
                    r.approved = false;

                // Only a complete observation may replace the repair list,
                // and it may replace it with nothing. A partial merge - a
                // serial sweep that learned only an address - has not looked
                // for problems and must not appear to have found none.
                if (device.observation_complete)
                    existing.repairs = device.repairs;
                else if (!device.repairs.empty())
                    existing.repairs = device.repairs;

                existing.observation_complete =
                    existing.observation_complete || device.observation_complete;

                return (int)i;
            }
        }

        m_devices.push_back(device);

        // The handle is the registry's to give, never the caller's. A record
        // arriving with one set - copied from an older list, say - would
        // otherwise alias a device that is still here.
        m_devices.back().handle = m_next_handle;
        m_next_handle = to_handle(to_number(m_next_handle) + 1);
        return (int)m_devices.size() - 1;
    }

    int Registry::unidentified_count() const
    {
        int n = 0;
        for (const auto& d : m_devices)
            if (!d.has_stable_identity()) n++;
        return n;
    }

    void Registry::clear()
    {
        m_devices.clear();
        m_selected = kNoHandle;

        // m_next_handle is deliberately NOT reset. A page still holding a
        // handle from before the clear must find nothing, not a new device
        // that happens to have been handed the same number.
    }

    int Registry::index_of(Handle handle) const
    {
        if (handle == kNoHandle)
            return -1;

        for (size_t i = 0; i < m_devices.size(); i++)
            if (m_devices[i].handle == handle)
                return (int)i;
        return -1;
    }

    void Registry::select(int index)
    {
        m_selected = (index >= 0 && index < (int)m_devices.size())
                     ? m_devices[index].handle
                     : kNoHandle;
    }

    bool Registry::select_by_handle(Handle handle)
    {
        if (index_of(handle) < 0)
        {
            // Clearing rather than leaving the previous selection in place.
            // A click on a device that is no longer there is a sign the page
            // is out of date, and continuing to show whatever was selected
            // before would hide that.
            m_selected = kNoHandle;
            return false;
        }

        m_selected = handle;
        return true;
    }

    int Registry::selected_index() const
    {
        return index_of(m_selected);
    }

    const DeviceRecord* Registry::selected() const
    {
        const int i = index_of(m_selected);
        return i < 0 ? nullptr : &m_devices[i];
    }

    bool Registry::approve_repair(Handle device, int repair_index)
    {
        const int d = index_of(device);
        if (d < 0)
            return false;

        auto& repairs = m_devices[d].repairs;
        if (repair_index < 0 || repair_index >= (int)repairs.size())
            return false;

        repairs[repair_index].approved = true;
        return true;
    }

    std::vector<std::pair<Handle, int>> Registry::pending_repairs() const
    {
        std::vector<std::pair<Handle, int>> out;
        for (const auto& d : m_devices)
            for (size_t r = 0; r < d.repairs.size(); r++)
                if (!d.repairs[r].approved)
                    out.push_back({ d.handle, (int)r });
        return out;
    }

    int Registry::refresh_duplicate_modbus_ids()
    {
        // Clear first. A conflict that has been resolved - because one of the
        // pair was renumbered or has gone - must stop being reported, and
        // re-deriving on top of the old list would stack repeats of the same
        // warning on every scan.
        for (auto& d : m_devices)
        {
            auto& r = d.repairs;
            r.erase(std::remove_if(r.begin(), r.end(), [](const Repair& x) {
                        return x.kind == RepairKind::ResolveDuplicateModbusId;
                    }),
                    r.end());
        }

        // Modbus id 0 is not an address, so several devices reporting it are
        // not in conflict with each other. Without this, every unconfigured
        // device on a subnet would accuse every other one.
        std::map<int, int> counts;
        for (const auto& d : m_devices)
            if (d.modbus_id_reported != 0)
                counts[d.modbus_id_reported]++;

        int flagged = 0;
        for (auto& d : m_devices)
        {
            const int id = d.modbus_id_reported;
            if (id == 0 || counts[id] < 2)
                continue;

            Repair repair;
            repair.kind    = RepairKind::ResolveDuplicateModbusId;
            repair.problem = "Modbus id " + std::to_string(id) + " is claimed by " +
                             std::to_string(counts[id]) +
                             " devices, so none of them can be addressed reliably.";
            repair.action  = "Write a free id to register 10 on this device, "
                             "leaving the others on " + std::to_string(id) + ".";
            repair.consequence =
                "This device moves to a new address. Anything that refers to it "
                "by the old id - other panels, schedules, third-party "
                "integrations - will need updating to match.";

            // Changing an id back is just another write, so this one is
            // undoable in a way that assigning a serial is not.
            repair.reversible = true;

            d.repairs.push_back(repair);
            flagged++;
        }

        return flagged;
    }

    const char* to_string(Provenance p)
    {
        switch (p)
        {
        case Provenance::ManuallyAdded:   return "added by hand";
        case Provenance::BacnetBroadcast: return "answered a broadcast";
        case Provenance::BacnetUnicast:   return "answered at a known address";
        case Provenance::SerialScan:      return "found by serial scan";
        case Provenance::Restored:        return "restored from the saved list";
        }
        return "unknown";
    }

    const char* to_string(RepairKind k)
    {
        switch (k)
        {
        case RepairKind::AssignSerialNumber:      return "assign a serial number";
        case RepairKind::ResolveDuplicateModbusId: return "resolve a duplicate Modbus id";
        case RepairKind::ConfigureGatewaySubPort: return "configure a gateway sub-port";
        }
        return "unknown repair";
    }
}
