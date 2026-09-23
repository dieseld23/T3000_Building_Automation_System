#include "registry.h"

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
        m_selected = -1;
    }

    void Registry::select(int index)
    {
        m_selected = (index >= 0 && index < (int)m_devices.size()) ? index : -1;
    }

    const DeviceRecord* Registry::selected() const
    {
        if (m_selected < 0 || m_selected >= (int)m_devices.size())
            return nullptr;
        return &m_devices[m_selected];
    }

    bool Registry::approve_repair(int device_index, int repair_index)
    {
        if (device_index < 0 || device_index >= (int)m_devices.size())
            return false;

        auto& repairs = m_devices[device_index].repairs;
        if (repair_index < 0 || repair_index >= (int)repairs.size())
            return false;

        repairs[repair_index].approved = true;
        return true;
    }

    std::vector<std::pair<int, int>> Registry::pending_repairs() const
    {
        std::vector<std::pair<int, int>> out;
        for (size_t d = 0; d < m_devices.size(); d++)
            for (size_t r = 0; r < m_devices[d].repairs.size(); r++)
                if (!m_devices[d].repairs[r].approved)
                    out.push_back({ (int)d, (int)r });
        return out;
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
