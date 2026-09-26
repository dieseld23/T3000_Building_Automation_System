#include "device_list.h"

#include <map>
#include <vector>

#include "../json/read.h"

namespace t5000::app
{
    namespace
    {
        using namespace t5000::device;

        const DeviceRecord* find(const Registry& registry, Handle handle)
        {
            if (handle == kNoHandle)
                return nullptr;
            for (const auto& d : registry.devices())
                if (d.handle == handle)
                    return &d;
            return nullptr;
        }

        const char* const kGone = "That device is no longer in the list. The page may be out of date.";

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

        // Characters, not bytes, counted as UTF-8 lead bytes. False on a
        // sequence that is not well-formed UTF-8, which SQLite would store
        // and the page would show as replacement characters.
        bool utf8_length(const std::string& s, int& chars)
        {
            chars = 0;
            size_t i = 0;
            while (i < s.size())
            {
                const unsigned char c = (unsigned char)s[i];
                size_t n = 0;
                unsigned cp = 0;
                if (c < 0x80)               { n = 1; cp = c; }
                else if ((c & 0xE0) == 0xC0) { n = 2; cp = c & 0x1F; }
                else if ((c & 0xF0) == 0xE0) { n = 3; cp = c & 0x0F; }
                else if ((c & 0xF8) == 0xF0) { n = 4; cp = c & 0x07; }
                else return false;

                if (i + n > s.size())
                    return false;
                for (size_t k = 1; k < n; k++)
                {
                    const unsigned char cc = (unsigned char)s[i + k];
                    if ((cc & 0xC0) != 0x80)
                        return false;
                    cp = (cp << 6) | (cc & 0x3F);
                }

                // Overlong forms, surrogates and values past U+10FFFF are
                // not characters.
                if ((n == 2 && cp < 0x80) || (n == 3 && cp < 0x800) || (n == 4 && cp < 0x10000) ||
                    (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF)
                    return false;

                i += n;
                chars++;
            }
            return true;
        }

        bool clean_field(std::string& value, const char* label, std::string& message)
        {
            trim(value);

            for (const char c : value)
            {
                if ((unsigned char)c < 0x20 || c == 0x7F)
                {
                    message = std::string("The ") + label + " holds a control character.";
                    return false;
                }
            }

            int chars = 0;
            if (!utf8_length(value, chars))
            {
                message = std::string("The ") + label + " is not valid text.";
                return false;
            }
            if (chars > kMaxPlacementChars)
            {
                message = std::string("The ") + label + " is " + std::to_string(chars) +
                          " characters long. The most kept is " +
                          std::to_string(kMaxPlacementChars) + ".";
                return false;
            }
            return true;
        }

        bool handle_from(const std::map<std::string, json::FlatValue>& fields, Handle& handle,
                         std::string& message)
        {
            const auto it = fields.find("handle");
            unsigned long long raw = 0;
            if (it == fields.end() || !json::parse_u64(it->second.text, raw) || raw == 0)
            {
                message = "handle must be a device's handle, a whole number above 0.";
                return false;
            }
            handle = to_handle(raw);
            return true;
        }

        // A whole number from 0 to `most`, given as a JSON number or as a
        // string of digits, which is how a text box sends one. The messages
        // are the page's, since they are shown to whoever filled in the form.
        bool number_from(const std::map<std::string, json::FlatValue>& fields, const char* key,
                         unsigned long long most, const char* missing, const char* wrong,
                         unsigned long long& target, std::string& message)
        {
            const auto it = fields.find(key);
            if (it == fields.end() || it->second.text.empty())
            {
                message = missing;
                return false;
            }

            unsigned long long n = 0;
            if (!json::parse_u64(it->second.text, n) || n > most)
            {
                message = wrong;
                return false;
            }
            target = n;
            return true;
        }

        bool is_known_product(ProductClassId id)
        {
            if (id == ProductClassId::Unknown)
                return false;

            const CapabilityTable table = known_products();
            for (int i = 0; i < table.count; i++)
                if (table.entries[i].id == id)
                    return true;
            return false;
        }

        bool string_from(const std::map<std::string, json::FlatValue>& fields, const char* key,
                         std::string& target, std::string& message)
        {
            const auto it = fields.find(key);
            if (it == fields.end())
            {
                target.clear();
                return true;
            }
            if (!it->second.is_string)
            {
                message = std::string(key) + " must be a string.";
                return false;
            }
            target = it->second.text;
            return true;
        }
    }

    StoreStatus open_saved_list(store::DeviceDb& db, const std::string& path, Registry& registry)
    {
        StoreStatus status;
        status.path = path;

        std::string error;
        if (!db.open(path, error))
        {
            status.error = error;
            return status;
        }

        std::vector<DeviceRecord> saved;
        if (!db.load(saved, error))
        {
            // Open but unreadable. Not written to either, since a list that
            // could not be read would be saved back without its devices'
            // names.
            db.close();
            status.error = "it could not be read: " + error;
            return status;
        }

        for (const auto& d : saved)
            registry.add_or_merge(d);

        status.saving   = true;
        status.restored = (int)saved.size();
        return status;
    }

    void record_scan(Registry& registry, store::DeviceDb& db, const discovery::ScanResult& result,
                     int64_t now, ScanSummary& summary, StoreStatus& status)
    {
        summary.stats = result.stats;
        summary.error = result.error;

        const int scan = registry.begin_scan();

        // Merged, not replaced. A controller that answered earlier and stayed
        // quiet this time is information worth keeping on screen; dropping it
        // would make a flaky device look like one that was never there.
        for (DeviceRecord d : result.devices)
        {
            d.answered_scan = scan;
            d.first_seen    = now;
            d.last_seen     = now;
            registry.add_or_merge(d);
        }

        // Over the WHOLE list, not just this scan. Two devices sharing an id
        // can answer on different scans and never appear in one result, and a
        // rescan that only one of a pair answers would otherwise leave the
        // other accusing a device the page now shows as clean.
        summary.stats.duplicate_modbus_ids = registry.refresh_duplicate_modbus_ids();

        if (!db.is_open())
            return;

        // Every device seen this session, not only those in this scan. A
        // save that failed earlier is then made good by the next one that
        // works, and clearing the error below is true rather than hopeful.
        // A device from an earlier scan keeps its own last_seen: the file
        // takes the later of the two.
        //
        // The merged records, not the raw ones: a field this scan did not
        // carry keeps what an earlier one learned, on disk as in memory.
        std::vector<DeviceRecord> answered;
        for (const auto& d : registry.devices())
            if (d.answered_scan != 0)
                answered.push_back(d);

        std::string error;
        if (db.save_scanned(answered, error))
            status.error.clear();
        else
            status.error = "the last scan could not be saved: " + error;
    }

    bool forget_device(Registry& registry, store::DeviceDb& db, Handle handle, ScanSummary& summary,
                       std::string& message)
    {
        const DeviceRecord* d = find(registry, handle);
        if (!d)
        {
            message = kGone;
            return false;
        }

        // A device with no serial was never saved, so there is nothing to
        // delete from the file.
        if (d->has_stable_identity() && db.is_open())
        {
            std::string error;
            if (!db.forget(d->serial_number, error))
            {
                message = "It could not be removed from the saved list: " + error;
                return false;
            }
        }

        registry.remove(handle);
        summary.stats.duplicate_modbus_ids = registry.refresh_duplicate_modbus_ids();
        return true;
    }

    bool forget_all(Registry& registry, store::DeviceDb& db, std::string& message)
    {
        if (db.is_open())
        {
            std::string error;
            if (!db.forget_all_scanned(error))
            {
                message = "The saved list could not be emptied: " + error;
                return false;
            }
        }

        registry.clear();
        return true;
    }

    bool place_device(Registry& registry, store::DeviceDb& db, Handle handle,
                      const Placement& placement, const StoreStatus& status, std::string& message)
    {
        const DeviceRecord* d = find(registry, handle);
        if (!d)
        {
            message = kGone;
            return false;
        }

        if (!d->has_stable_identity())
        {
            message = "This device reports no serial number, so it cannot be saved, and a name "
                      "given to it could not be kept.";
            return false;
        }

        if (!db.is_open())
        {
            message = "The device list is not being saved";
            if (!status.error.empty())
                message += " (" + status.error + ")";
            message += ", so a name given now would be lost when T5000 closes.";
            return false;
        }

        Placement cleaned = placement;
        if (!clean_placement(cleaned, message))
            return false;

        DeviceRecord updated = *d;
        updated.placement = cleaned;

        std::string error;
        if (!db.save_placement(updated, error))
        {
            message = "It could not be saved: " + error;
            return false;
        }

        registry.set_placement(handle, cleaned);
        return true;
    }

    bool add_device(Registry& registry, store::DeviceDb& db, const HandAdded& device,
                    const StoreStatus& status, Handle& handle, std::string& message)
    {
        handle = kNoHandle;

        // 0 and 0xFFFFFFFF are what a device with no serial reports. A scan
        // can never match an entry on one, so the entry could never become
        // the device it stands for.
        if (is_uninitialised_serial(device.serial))
        {
            message = "The serial number must be from 1 to 4294967294. " + std::to_string(device.serial) +
                      " is what a device with no serial reports, so a scan could never match it to "
                      "this entry.";
            return false;
        }

        if (!is_known_product(device.product))
        {
            message = "Product " + std::to_string((int)static_cast<uint8_t>(device.product)) +
                      " is not one T5000 has been taught about. Pick one from the list.";
            return false;
        }

        // In the table, but not a Temco device: T5000's scan never reports a
        // third-party device's serial, so an entry for one could never be
        // matched to the device it stands for.
        if (device.product == ProductClassId::ThirdPartyDevice)
        {
            message = "A third-party device cannot be added by hand: a scan never reports its "
                      "serial, so the entry could never be matched to the device.";
            return false;
        }

        // One of the models T3000 names for this product, or 0 for a model
        // not known. Any other pair would give the entry a panel type no such
        // device has, and a count of points to match.
        if (device.mini_type != 0 && !find_model(device.product, device.mini_type))
        {
            message = "Panel type " + std::to_string(device.mini_type) + " is not a model of the " +
                      std::string(to_string(device.product)) + " that T5000 knows. Pick one from the list.";
            return false;
        }

        // Refused, not merged. Merging would put the product typed here over
        // the one a device reported, and an entry for a device already listed
        // has nothing to add that Edit does not.
        for (const auto& d : registry.devices())
        {
            if (d.serial_number != device.serial)
                continue;

            message = "Serial " + std::to_string(device.serial) + " is already in the list";
            if (d.provenance == Provenance::ManuallyAdded)
                message += ", added by hand";
            else if (!d.placement.name.empty())
                message += ", as \"" + d.placement.name + "\"";
            message += ". Use Edit on that row to name or place it.";
            return false;
        }

        if (!db.is_open())
        {
            message = "The device list is not being saved";
            if (!status.error.empty())
                message += " (" + status.error + ")";
            message += ", so a device added now would be lost when T5000 closes.";
            return false;
        }

        Placement cleaned = device.placement;
        if (!clean_placement(cleaned, message))
            return false;

        // Only what the operator gave. No address: nothing has answered from
        // one, and the Inputs page refuses a device added by hand before it
        // looks for one. Not reached, and not a complete observation, so a
        // scan that finds the serial replaces everything here but the name
        // and location.
        DeviceRecord d;
        d.serial_number        = device.serial;
        d.product              = device.product;
        d.mini_type            = device.mini_type;
        d.provenance           = Provenance::ManuallyAdded;
        d.reached              = false;
        d.observation_complete = false;
        d.placement            = cleaned;

        std::string error;
        if (!db.add_by_hand(d, error))
        {
            message = "It could not be saved: " + error + ".";
            return false;
        }

        const int index = registry.add_or_merge(d);
        handle = registry.devices()[index].handle;
        return true;
    }

    bool read_add_request(const std::string& body, HandAdded& device, std::string& message)
    {
        std::map<std::string, json::FlatValue> fields;
        std::string error;
        if (!json::parse_flat_object(body, fields, error))
        {
            message = "The request could not be read: " + error + ".";
            return false;
        }

        unsigned long long product   = 0;
        unsigned long long mini_type = 0;
        unsigned long long serial    = 0;
        HandAdded d;

        // Left out or empty is a model not known, which is panel type 0.
        const auto model = fields.find("miniType");
        const bool model_given = model != fields.end() && !model->second.text.empty();

        if (!number_from(fields, "productId", 255, "Choose a model.",
                         "The product must be a product number from 0 to 255.", product, message) ||
            (model_given && !number_from(fields, "miniType", 255, "", "The model must be one from the list.",
                                         mini_type, message)) ||
            !number_from(fields, "serialNumber", 0xFFFFFFFFull, "Enter the device's serial number.",
                         "The serial number must be a whole number, digits only, up to 4294967294.",
                         serial, message) ||
            !string_from(fields, "name", d.placement.name, message) ||
            !string_from(fields, "building", d.placement.building, message) ||
            !string_from(fields, "floor", d.placement.floor, message) ||
            !string_from(fields, "room", d.placement.room, message))
            return false;

        d.product   = static_cast<ProductClassId>((uint8_t)product);
        d.mini_type = (int)mini_type;
        d.serial    = (uint32_t)serial;
        device      = d;
        return true;
    }

    bool clean_placement(Placement& placement, std::string& message)
    {
        return clean_field(placement.name, "name", message) &&
               clean_field(placement.building, "building", message) &&
               clean_field(placement.floor, "floor", message) &&
               clean_field(placement.room, "room", message);
    }

    bool read_handle_request(const std::string& body, Handle& handle, std::string& message)
    {
        std::map<std::string, json::FlatValue> fields;
        std::string error;
        if (!json::parse_flat_object(body, fields, error))
        {
            message = "The request could not be read: " + error + ".";
            return false;
        }
        return handle_from(fields, handle, message);
    }

    bool read_placement_request(const std::string& body, Handle& handle, Placement& placement,
                                std::string& message)
    {
        std::map<std::string, json::FlatValue> fields;
        std::string error;
        if (!json::parse_flat_object(body, fields, error))
        {
            message = "The request could not be read: " + error + ".";
            return false;
        }

        Placement p;
        if (!handle_from(fields, handle, message) ||
            !string_from(fields, "name", p.name, message) ||
            !string_from(fields, "building", p.building, message) ||
            !string_from(fields, "floor", p.floor, message) ||
            !string_from(fields, "room", p.room, message))
            return false;

        placement = p;
        return true;
    }
}
