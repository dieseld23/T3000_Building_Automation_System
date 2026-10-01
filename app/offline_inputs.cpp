#include "offline_inputs.h"

#include <algorithm>
#include <map>

#include "../device/input_rows.h"
#include "../json/read.h"
#include "../offline/input_cells.h"
#include "../offline/input_ranges.h"
#include "../offline/prog_file.h"
#include "../wire/decode.h"
#include "points_json.h"

namespace t5000::app
{
    namespace
    {
        using namespace t5000::device;

        const char* const kGone = "That device is no longer in the list. The page may be out of date.";

        const DeviceRecord* find(const Registry& registry, Handle handle)
        {
            if (handle == kNoHandle)
                return nullptr;
            for (const auto& d : registry.devices())
                if (d.handle == handle)
                    return &d;
            return nullptr;
        }

        bool has_models(ProductClassId product)
        {
            const ModelTable models = known_models();
            for (int i = 0; i < models.count; i++)
                if (models.entries[i].product == product)
                    return true;
            return false;
        }

        std::string not_saving(const StoreStatus& status)
        {
            std::string message = "The device list is not being saved";
            if (!status.error.empty())
                message += " (" + status.error + ")";
            return message + ", so a change made now would be lost when T5000 closes.";
        }

        bool read_fields(const std::string& body, std::map<std::string, json::FlatValue>& fields, std::string& message)
        {
            std::string error;
            if (!json::parse_flat_object(body, fields, error))
            {
                message = "The request could not be read: " + error + ".";
                return false;
            }
            return true;
        }

        bool handle_from(const std::map<std::string, json::FlatValue>& fields, Handle& handle, std::string& message)
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

        // 0-based, as the payload's "index". A point's index is one byte.
        bool index_from(const std::map<std::string, json::FlatValue>& fields, int& index, std::string& message)
        {
            const auto it = fields.find("index");
            unsigned long long raw = 0;
            if (it == fields.end() || !json::parse_u64(it->second.text, raw) || raw > 254)
            {
                message = "index must be an input's index, a whole number from 0 to 254.";
                return false;
            }
            index = (int)raw;
            return true;
        }

        std::string input_list(const std::vector<int>& indexes)
        {
            // "input 3", "inputs 3, 7 and 12", or the first few and a count,
            // numbered as the grid numbers them.
            constexpr size_t kNamed = 6;
            std::string out = indexes.size() == 1 ? "input " : "inputs ";
            const size_t named = std::min(indexes.size(), kNamed);
            for (size_t i = 0; i < named; i++)
            {
                if (i != 0)
                    out += (i + 1 == named && indexes.size() <= kNamed) ? " and " : ", ";
                out += std::to_string(indexes[i] + 1);
            }
            if (indexes.size() > kNamed)
                out += " and " + std::to_string(indexes.size() - kNamed) + " more";
            return out;
        }
    }

    bool is_configured_offline(const DeviceRecord& d)
    {
        return d.provenance == Provenance::ManuallyAdded || d.is_virtual();
    }

    OfflineInputsPlan plan_offline_inputs(const DeviceRecord& d)
    {
        OfflineInputsPlan plan;

        if (!is_configured_offline(d))
        {
            plan.reason = "This device has been found, by a scan or by Find, so its inputs are read from it, not "
                          "configured offline.";
            return plan;
        }

        // The products whose inputs T3000 reads as Str_in_point, one struct
        // each: those it reads by private transfer. The rest are registers,
        // and a Tstat's are a different model altogether.
        const Capabilities& cap = capabilities(d.product);
        if (cap.path != DataPath::BacnetPrivateData)
        {
            plan.reason = std::string("Inputs are configured offline only on the controllers T5000 reads by BACnet "
                                      "private transfer: the CM5, MiniPanel, MiniPanel ARM, T3 Series (ESP32) and "
                                      "TSTAT10. The ") +
                          cap.name + " is not one of them yet.";
            return plan;
        }

        const PanelResolution panel = resolve_chosen_panel(d.product, d.mini_type);
        if (!panel.resolved)
        {
            if (has_models(d.product))
            {
                plan.reason = "Choose its model first, with Edit on the Devices page. The model decides how T3000 "
                              "shows some inputs, and which ranges can be changed, so its inputs are not "
                              "configured without it.";
            }
            else
            {
                plan.reason = std::string("T3000 names no model of the ") + cap.name +
                              " to choose, and the model decides how T3000 shows some inputs, so its inputs "
                              "are not configured offline.";
            }
            return plan;
        }

        // Nothing has been read, so there are no settings to size the lists
        // from: firmware 0 and the chosen panel type, which is how T3000 sizes
        // a panel it has not read (64 inputs, Initial_All_Point).
        wire::PanelSettings settings;
        settings.mini_type_byte = (uint8_t)static_cast<uint8_t>(panel.type);

        plan.product = d.product;
        plan.type    = panel.type;
        plan.model   = panel_name(d.product, panel.type);
        plan.inputs = inputs_to_read(d.product, settings);

        const InputRows rows = input_rows(d.product, settings);
        plan.rows = rows.set && rows.rows < plan.inputs ? rows.rows : plan.inputs;

        plan.can_edit = true;
        return plan;
    }

    offline::InputPanel OfflineInputsPlan::panel() const
    {
        offline::InputPanel p;
        p.product = product;
        p.type    = type;
        p.rows    = rows;
        return p;
    }

    bool load_offline_inputs(store::DeviceDb& db, const DeviceRecord& d, const OfflineInputsPlan& plan,
                             OfflineInputs& out, std::string& error)
    {
        out = OfflineInputs();
        for (int i = 0; i < plan.inputs; i++)
            out.inputs.push_back(offline::default_input(i));
        out.bases = out.inputs;

        // No file, no changes: every input as T3000 starts it.
        if (!db.is_open())
            return true;

        std::vector<store::OfflinePoint> saved;
        if (!db.load_offline_inputs(store::key_of(d), saved, error))
            return false;

        for (const auto& p : saved)
        {
            if (p.index < 0 || p.index >= plan.inputs)
            {
                out.beyond++;
                continue;
            }

            // The table holds only whole inputs; a row that is not one was
            // not written by T5000, and is not guessed at.
            if (p.base.size() != wire::kInputPointWireSize || p.edited.size() != wire::kInputPointWireSize)
            {
                error = "the change saved for input " + std::to_string(p.index + 1) + " is not one input";
                return false;
            }
            std::copy(p.edited.begin(), p.edited.end(), out.inputs[(size_t)p.index].begin());
            std::copy(p.base.begin(), p.base.end(), out.bases[(size_t)p.index].begin());
        }
        return true;
    }

    std::string offline_inputs_payload(store::DeviceDb& db, const StoreStatus& status, const DeviceRecord& d)
    {
        const std::string nothing_sent =
            d.is_virtual() ? std::string("There is no device to read: this is a virtual device, a configuration "
                                         "with no device behind it. Nothing was sent.")
                           : "There is no device to read: this entry was added by hand, and no scan has found a "
                             "device with serial " +
                                 std::to_string(d.serial_number) + ". Nothing was sent.";

        const OfflineInputsPlan plan = plan_offline_inputs(d);
        if (!plan.can_edit)
        {
            std::string reason = nothing_sent + " " + plan.reason;

            // Changes saved under a model since unset, say, are kept, not
            // dropped, and the page says so.
            std::vector<int> saved;
            std::string ignored;
            if (db.is_open() && offline_input_indexes(db, store::key_of(d), saved, ignored) && !saved.empty())
            {
                reason += " Changes to " + input_list(saved) + " made earlier are kept in T5000's list.";
            }
            return build_unavailable_inputs_json(d.serial_number, d.address_note, reason);
        }

        OfflineInputs config;
        std::string error;
        if (!load_offline_inputs(db, d, plan, config, error))
        {
            return build_unavailable_inputs_json(d.serial_number, d.address_note,
                                                 nothing_sent + " The inputs T5000 keeps for it could not be "
                                                                "read from its list: " + error + ".");
        }

        DeviceInfo info;
        info.serial_number = d.serial_number;
        info.product_id    = (int)static_cast<uint8_t>(d.product);

        std::vector<wire::InputPoint> points((size_t)plan.inputs);
        for (size_t i = 0; i < points.size(); i++)
            wire::decode_input_point(config.inputs[i].data(), wire::kInputPointWireSize, points[i]);

        OfflineInputsView view;
        view.handle = to_number(d.handle);
        view.model  = plan.model;
        view.type   = plan.type;
        view.saving = status.saving && db.is_open();
        view.rows   = (size_t)plan.rows;
        for (const auto& c : offline::input_range_choices())
            view.range_choices.push_back({ c.number, offline::input_range_name(c) });
        view.signal_types = offline::signal_type_choices();

        const offline::InputPanel panel = plan.panel();
        for (size_t i = 0; i < config.inputs.size(); i++)
        {
            const offline::InputBytes& p = config.inputs[i];
            view.changed.push_back(offline::changed_fields(config.bases[i], p));
            if (!view.changed.back().empty())
                view.edited++;

            std::vector<std::string> editable;
            bool value = false;
            for (const auto f : offline::editable_input_fields(panel, (int)i, p))
            {
                editable.push_back(offline::input_field_name(f));
                value = value || f == offline::InputField::Value;
            }
            view.editable.push_back(editable);

            std::string next;
            view.value_toggles.push_back(value && offline::input_value_toggle(p, next) ? next : std::string());

            view.range_numbers.push_back(
                offline::input_range_number(p[offline::input_at::digital_analog], p[offline::input_at::range]));
            view.ranges.push_back(offline::input_ranges_offered(plan.product, plan.type, (int)i));
        }
        view.range_note = "These are the ranges T3000's Range dialog offers this input of a " + plan.model +
                          ". It offers PT 1K only on a panel whose settings say it has a PT 1K input, and custom "
                          "ranges only once their names are read, so neither is listed here.";

        if (d.is_virtual())
        {
            view.note = "Serial " + std::to_string(d.serial_number) +
                        " is a virtual device: a configuration with no device behind it. These are the inputs "
                        "T5000 keeps for it: each as T3000 starts a new panel's, with the changes made here. They "
                        "are saved in T5000's device list, and reach a device only once copied to one, which "
                        "T5000 cannot do yet.";
        }
        else
        {
            view.note = "Serial " + std::to_string(d.serial_number) +
                        " was added by hand and has not been found. These are the inputs T5000 keeps for it: "
                        "each as T3000 starts a new panel's, with the changes made here. They are saved in T5000's "
                        "device list, and nothing writes them to the device yet.";
        }
        if (!view.saving)
            view.note += " " + not_saving(status);

        view.detail = "Nothing is read from this device or sent to it.";
        view.panel_note = "Shown as T3000 shows the inputs of a " + plan.model +
                          ". No custom range names are known, since none were read.";
        if (config.beyond != 0)
        {
            view.panel_note += " " + std::to_string(config.beyond) +
                               (config.beyond == 1 ? " input changed earlier is" : " inputs changed earlier are") +
                               " past this model's " + std::to_string(plan.inputs) +
                               ". The changes are kept, and not shown.";
        }

        return build_offline_inputs_json(info, points, view);
    }

    bool read_input_edit_request(const std::string& body, InputEditRequest& request, std::string& message)
    {
        std::map<std::string, json::FlatValue> fields;
        if (!read_fields(body, fields, message))
            return false;

        InputEditRequest r;
        if (!handle_from(fields, r.handle, message) || !index_from(fields, r.index, message))
            return false;

        const auto field = fields.find("field");
        if (field == fields.end() || !field->second.is_string ||
            !offline::input_field_from_name(field->second.text, r.field))
        {
            std::string names;
            for (const auto f : offline::input_fields())
                names += std::string(names.empty() ? "" : ", ") + offline::input_field_name(f);
            message = "field must be one of " + names + ".";
            return false;
        }

        const auto value = fields.find("value");
        if (value == fields.end() || !value->second.is_string)
        {
            message = "value must be a string.";
            return false;
        }
        r.value = value->second.text;

        request = r;
        return true;
    }

    bool read_input_revert_request(const std::string& body, Handle& handle, int& index, std::string& message)
    {
        std::map<std::string, json::FlatValue> fields;
        if (!read_fields(body, fields, message))
            return false;

        Handle h = kNoHandle;
        int i = -1;
        if (!handle_from(fields, h, message) || !index_from(fields, i, message))
            return false;

        handle = h;
        index  = i;
        return true;
    }

    bool edit_offline_input(const Registry& registry, store::DeviceDb& db, const StoreStatus& status,
                            const InputEditRequest& request, std::string& message)
    {
        const DeviceRecord* d = find(registry, request.handle);
        if (!d)
        {
            message = kGone;
            return false;
        }

        const OfflineInputsPlan plan = plan_offline_inputs(*d);
        if (!plan.can_edit)
        {
            message = plan.reason;
            return false;
        }

        if (!db.is_open())
        {
            message = not_saving(status);
            return false;
        }

        OfflineInputs config;
        std::string error;
        if (!load_offline_inputs(db, *d, plan, config, error))
        {
            message = "The inputs T5000 keeps for this device could not be read from its list: " + error + ".";
            return false;
        }

        bool changed = false;
        if (!offline::apply_input_edit(config.inputs, plan.panel(), request.index, request.field, request.value,
                                       changed, message))
            return false;

        // Already so: nothing to save, as T3000 writes nothing.
        if (!changed)
            return true;

        store::OfflinePoint point;
        point.index = request.index;
        point.base.assign(config.bases[(size_t)request.index].begin(), config.bases[(size_t)request.index].end());
        point.edited.assign(config.inputs[(size_t)request.index].begin(), config.inputs[(size_t)request.index].end());
        if (!db.save_offline_input(store::key_of(*d), point, error))
        {
            message = "The change could not be saved: " + error + ".";
            return false;
        }
        return true;
    }

    bool revert_offline_input(const Registry& registry, store::DeviceDb& db, const StoreStatus& status,
                              Handle handle, int index, std::string& message)
    {
        const DeviceRecord* d = find(registry, handle);
        if (!d)
        {
            message = kGone;
            return false;
        }

        // Only where the changes can be seen: a device that has been found
        // shows what it holds, not these.
        if (!is_configured_offline(*d))
        {
            message = "This device has been found, by a scan or by Find, so its inputs are read from it. Changes "
                      "made offline before then are kept until T5000 can write them.";
            return false;
        }

        if (!db.is_open())
        {
            message = not_saving(status);
            return false;
        }

        std::string error;
        if (!db.revert_offline_input(store::key_of(*d), index, error))
        {
            message = "The input could not be put back: " + error + ".";
            return false;
        }
        return true;
    }

    bool read_input_import_request(const std::string& body, InputImportRequest& request, std::string& message)
    {
        std::map<std::string, json::FlatValue> fields;
        if (!read_fields(body, fields, message))
            return false;

        InputImportRequest r;
        if (!handle_from(fields, r.handle, message))
            return false;

        const auto file = fields.find("file");
        if (file == fields.end() || !file->second.is_string || !offline::base64_decode(file->second.text, r.file))
        {
            message = "file must be the .prog file, in base64.";
            return false;
        }

        const auto check = fields.find("check");
        if (check != fields.end())
        {
            if (check->second.is_string || (check->second.text != "true" && check->second.text != "false"))
            {
                message = "check must be true or false.";
                return false;
            }
            r.check = check->second.text == "true";
        }

        request = r;
        return true;
    }

    bool import_offline_inputs(const Registry& registry, store::DeviceDb& db, const StoreStatus& status,
                               const InputImportRequest& request, std::string& message)
    {
        const DeviceRecord* d = find(registry, request.handle);
        if (!d)
        {
            message = kGone;
            return false;
        }

        const OfflineInputsPlan plan = plan_offline_inputs(*d);
        if (!plan.can_edit)
        {
            message = plan.reason;
            return false;
        }

        if (!db.is_open())
        {
            message = not_saving(status);
            return false;
        }

        offline::ProgFile file;
        if (!offline::read_prog_file(request.file.data(), request.file.size(), file, message))
            return false;

        // Matched by the serial in its settings (the owner's decision A,
        // 2026-09-26), and then by the model, which decides what each
        // input's bytes mean. A virtual device's serial is one T5000 handed
        // out, and in no file, so it takes a file by the model alone (the
        // owner's decision, 2026-09-27).
        const uint32_t serial = file.settings.serial_number;
        if (!d->is_virtual())
        {
            if (serial == 0)
            {
                message = "This .prog file does not say which device it was saved from: the serial in its "
                          "settings is 0. A file's inputs are imported only into the device it was saved from.";
                return false;
            }
            if (serial != d->serial_number)
            {
                message = "This .prog file was saved from serial " + std::to_string(serial) +
                          ", and this device is serial " + std::to_string(d->serial_number) +
                          ". A file's inputs are imported only into the device it was saved from.";
                return false;
            }
        }

        const int type = file.settings.mini_type();
        if (type != (int)static_cast<uint8_t>(plan.type))
        {
            if (type == 0)
            {
                message = "This .prog file's settings give no panel type, so they do not say it was saved from a " +
                          plan.model + ". Its inputs are imported only into the model it was saved from.";
            }
            else if (!find_model(d->product, type))
            {
                message = "This .prog file gives panel type " + std::to_string(type) + ", which is not a model of the " +
                          std::string(to_string(d->product)) + " that T5000 knows, and this device is a " +
                          plan.model + ". Its inputs are imported only into the model it was saved from.";

                // T3000 numbered TSTAT11 27 until 2026-09-28 (device/product.h),
                // so a TSTAT11's file saved before then gives 27. Refused all the
                // same: 27 is also the ESP32 firmware's LSW sensor.
                if (d->product == ProductClassId::Esp32T3Series && plan.type == MiniType::Tstat11 && type == 27)
                    message += " T3000 numbered TSTAT11 27 until 2026-09-28, and 31 since, so a file saved from a "
                               "TSTAT11 before then gives 27.";
            }
            else
            {
                message = std::string("This .prog file was saved from a ") +
                          panel_name(d->product, static_cast<MiniType>(type)) + ", and this device is a " +
                          plan.model + ". Its inputs are imported only into the model it was saved from.";
            }
            return false;
        }

        std::vector<int> earlier;
        std::string error;
        if (!offline_input_indexes(db, store::key_of(*d), earlier, error))
        {
            message = "The inputs T5000 keeps for this device could not be read from its list: " + error + ".";
            return false;
        }

        // The rows T3000 shows for the model, as for a change; the file's
        // others are counted, and not kept.
        const offline::ImportedInputs imported = offline::imported_inputs(file, plan.rows);
        std::vector<store::OfflinePoint> points;
        for (const auto& [index, kept] : imported.inputs)
        {
            const offline::InputBytes base = offline::default_input(index);
            store::OfflinePoint point;
            point.index = index;
            point.base.assign(base.begin(), base.end());
            point.edited.assign(kept.begin(), kept.end());
            points.push_back(point);
        }
        const int past = imported.past;

        const std::string of_rows = " of this device's " + std::to_string(plan.rows) + " inputs";
        const std::string set = points.empty()
                                    ? "none" + of_rows + " differ from how T3000 starts them"
                                    : std::to_string(points.size()) + of_rows +
                                          (points.size() == 1 ? " is" : " are") + " set as the file has "
                                          + (points.size() == 1 ? "it" : "them");
        std::string replaced;
        if (!earlier.empty())
            replaced = " It replaces the changes made here to " + input_list(earlier) + ".";
        std::string beyond;
        if (past != 0)
        {
            beyond = " The file holds " + std::to_string(past) + (past == 1 ? " input" : " inputs") +
                     " past the " + std::to_string(plan.rows) + " a " + plan.model + " shows that " +
                     (past == 1 ? "is" : "are") + " not as T3000 starts " + (past == 1 ? "it" : "them") +
                     "; " + (past == 1 ? "it is" : "they are") + " not kept.";
        }
        const std::string kept_note =
            " Of each input, what the operator sets is kept: the labels, Auto/Manual, range, filter, calibration "
            "and signal type, and the value of an input in Manual. Its status, its external module, and the value "
            "of an input in Auto are what the panel read, and are not. Nothing else in the file is kept: not its "
            "outputs, variables, programs, schedules or settings. Nothing is sent to any device.";

        if (request.check)
        {
            const std::string from =
                d->is_virtual()
                    ? "was saved from a " + plan.model +
                          (serial != 0 ? ", serial " + std::to_string(serial) : std::string(", with no serial")) +
                          ". A virtual device takes a file saved from any " + plan.model +
                          ", so the file's serial is not compared."
                    : "was saved from serial " + std::to_string(serial) + ", a " + plan.model + ".";
            message = "This .prog file (version " + std::to_string(file.version) + ") " + from +
                      " If it is imported, " + set + "." + replaced + beyond + kept_note;
            return true;
        }

        if (!db.replace_offline_inputs(store::key_of(*d), points, error))
        {
            message = "The file's inputs could not be saved: " + error + ".";
            return false;
        }

        message = "Imported: " + set + "." + (earlier.empty() ? std::string() : " The changes made here before "
                  "are replaced.") + beyond;
        return true;
    }

    bool read_input_export_request(const std::string& body, InputExportRequest& request, std::string& message)
    {
        std::map<std::string, json::FlatValue> fields;
        if (!read_fields(body, fields, message))
            return false;

        InputExportRequest r;
        if (!handle_from(fields, r.handle, message))
            return false;

        const auto check = fields.find("check");
        if (check != fields.end())
        {
            if (check->second.is_string || (check->second.text != "true" && check->second.text != "false"))
            {
                message = "check must be true or false.";
                return false;
            }
            r.check = check->second.text == "true";
        }

        request = r;
        return true;
    }

    bool export_offline_inputs(const Registry& registry, store::DeviceDb& db, const StoreStatus& status,
                               const InputExportRequest& request, std::string& message, std::vector<uint8_t>& file,
                               std::string& name)
    {
        file.clear();
        name.clear();

        const DeviceRecord* d = find(registry, request.handle);
        if (!d)
        {
            message = kGone;
            return false;
        }

        const OfflineInputsPlan plan = plan_offline_inputs(*d);
        if (!plan.can_edit)
        {
            message = plan.reason;
            return false;
        }

        if (!db.is_open())
        {
            message = not_saving(status);
            return false;
        }

        OfflineInputs config;
        std::string error;
        if (!load_offline_inputs(db, *d, plan, config, error))
        {
            message = "The inputs T5000 keeps for this device could not be read from its list: " + error + ".";
            return false;
        }

        offline::ProgExport device;
        device.serial    = d->serial_number;
        device.mini_type = (uint8_t)static_cast<int>(plan.type);
        device.inputs    = config.inputs;
        const std::vector<uint8_t> bytes = offline::write_prog_file(device);

        if (request.check)
        {
            message = offline::describe_prog_export(bytes, plan.model) + " Nothing is sent to any device.";
            return true;
        }

        file    = bytes;
        name    = std::to_string(d->serial_number) + ".prog";
        message = "Exported as " + name + ", with this device's inputs and T3000's defaults for everything else.";
        return true;
    }

    std::string pending_offline_note(store::DeviceDb& db, const DeviceRecord& d)
    {
        std::vector<int> saved;
        std::string error;
        if (!db.is_open() || !d.has_stable_identity() || !offline_input_indexes(db, store::key_of(d), saved, error))
            return std::string();
        if (saved.empty())
            return std::string();

        return "Changes to " + input_list(saved) +
               " were made offline, before this device was found. They are kept in T5000's device list, "
               "and not written: T5000 cannot write to a device yet.";
    }

    bool offline_input_indexes(store::DeviceDb& db, const store::DeviceKey& key, std::vector<int>& indexes,
                               std::string& error)
    {
        indexes.clear();
        std::vector<store::OfflinePoint> saved;
        if (!db.load_offline_inputs(key, saved, error))
            return false;
        for (const auto& p : saved)
            indexes.push_back(p.index);
        return true;
    }
}
