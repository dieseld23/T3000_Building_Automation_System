#include "product_json.h"
#include "points_json.h"
#include "../device/product.h"

namespace t5000::app
{
    namespace
    {
        using namespace t5000::device;

        void append_screens(std::string& out, Screen s)
        {
            struct Named { Screen bit; const char* name; };
            static const Named kAll[] = {
                { Screen::Inputs,      "inputs" },
                { Screen::Outputs,     "outputs" },
                { Screen::Variables,   "variables" },
                { Screen::Programs,    "programs" },
                { Screen::Controllers, "controllers" },
                { Screen::Weekly,      "weekly" },
                { Screen::Annual,      "annual" },
                { Screen::Monitor,     "monitor" },
                { Screen::AlarmLog,    "alarmLog" },
                { Screen::Tstat,       "tstat" },
                { Screen::Settings,    "settings" },
                { Screen::UserLogin,   "userLogin" },
                { Screen::RemotePoint, "remotePoint" },
                { Screen::Array,       "array" },
                { Screen::Pvar,        "pvar" },
            };

            out += '[';
            bool first = true;
            for (const auto& n : kAll)
            {
                if (!has(s, n.bit)) continue;
                if (!first) out += ',';
                out += '"';
                out += n.name;
                out += '"';
                first = false;
            }
            out += ']';
        }

        void append_capabilities(std::string& out, const Capabilities& c)
        {
            out += "{\"id\":" + std::to_string(static_cast<int>(c.id));
            out += ",\"name\":\"" + json_escape(c.name) + "\"";
            out += ",\"dataPath\":\"" + json_escape(to_string(c.path)) + "\"";
            out += ",\"support\":\"" + json_escape(to_string(c.support)) + "\"";

            // Whether the tool can do anything with this product at all. The
            // page needs this as a boolean; deriving it from the support
            // string in JavaScript would put the rule in two places.
            out += ",\"usable\":";
            out += is_supported(c.id) ? "true" : "false";

            out += ",\"screens\":";
            append_screens(out, c.screens);

            out += ",\"note\":";
            if (c.note) out += "\"" + json_escape(c.note) + "\"";
            else        out += "null";

            out += '}';
        }
    }

    std::string build_products_json()
    {
        const auto table = known_products();

        std::string out = "{\"products\":[";
        for (int i = 0; i < table.count; i++)
        {
            if (i) out += ',';
            append_capabilities(out, table.entries[i]);
        }
        out += "],\"count\":" + std::to_string(table.count);

        // Said explicitly so nobody reads the list as "these are all the
        // products that exist". It is what the TABLE covers; ProductModel.h
        // has far more, and an absent one is unverified, not nonexistent.
        out += ",\"note\":\"the products this tool has been taught about - "
               "T3000 recognises many more, and one absent from this list is "
               "unverified rather than unsupported\"}";
        return out;
    }

    std::string build_product_json(int product_class_id, int mini_type)
    {
        const auto id = static_cast<ProductClassId>(product_class_id & 0xFF);
        const auto& c = capabilities(id);

        std::string out = "{\"product\":";
        append_capabilities(out, c);

        // The panel type is a separate axis and is reported separately, so the
        // page cannot accidentally present one as the other. Resolved through
        // resolve_panel rather than read directly, because mini_type 0 means
        // either "CM5" or "unconfigured" and only the hardware id can tell
        // them apart - see the note on resolve_panel.
        const auto r     = resolve_panel(id, mini_type);
        const auto info  = mini_type_info(r.type);
        const auto count = r.counts;

        out += ",\"panel\":{\"miniType\":" + std::to_string(mini_type);
        out += ",\"name\":\"" + json_escape(to_string(r.type)) + "\"";
        out += ",\"resolved\":";
        out += r.resolved ? "true" : "false";
        out += ",\"reason\":\"" + json_escape(r.reason) + "\"";

        const char* support = "counts from the init chain";
        switch (info.support)
        {
        case MiniTypeSupport::CountsFromInitChain: break;
        case MiniTypeSupport::RowMaskOnTstat10:    support = "a TSTAT10 with rows hidden"; break;
        case MiniTypeSupport::InputsOnly:          support = "inputs only"; break;
        case MiniTypeSupport::OutputsOnly:         support = "outputs only"; break;
        case MiniTypeSupport::NotImplemented:      support = "never implemented in T3000"; break;
        }
        out += ",\"support\":\"" + json_escape(support) + "\"";

        if (info.support == MiniTypeSupport::RowMaskOnTstat10)
        {
            out += ",\"hiddenRows\":{\"first\":" + std::to_string(info.hidden_row_first) +
                   ",\"last\":" + std::to_string(info.hidden_row_last) + "}";
        }

        out += ",\"note\":";
        if (info.note) out += "\"" + json_escape(info.note) + "\"";
        else           out += "null";

        // known=false is reported as null counts rather than zeros. Zero
        // points and unknown points look identical in a grid, and only one of
        // them means "do not trust this screen".
        out += ",\"points\":";
        if (count.known)
        {
            out += "{\"analogInputs\":"   + std::to_string(count.analog_inputs) +
                   ",\"digitalInputs\":"  + std::to_string(count.digital_inputs) +
                   ",\"analogOutputs\":"  + std::to_string(count.analog_outputs) +
                   ",\"digitalOutputs\":" + std::to_string(count.digital_outputs) +
                   ",\"inputs\":"         + std::to_string(count.inputs()) +
                   ",\"outputs\":"        + std::to_string(count.outputs()) + "}";
        }
        else
        {
            out += "null";
        }

        out += "}}";
        return out;
    }
}
