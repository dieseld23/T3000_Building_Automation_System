// T5000 - a standalone configuration tool for Tstat/T3 units.
//
//   T5000.exe --selftest    run the self-tests, exit non-zero on failure
//   T5000.exe               serve the UI on http://127.0.0.1:8730
//
// It can now FIND devices, and still cannot change one. The scan is read-only
// by construction (see discovery/scanner.h), and problems it notices are
// staged as proposals nobody has agreed to yet. Point data is still the
// fixture, flagged as such everywhere it is served.

#include <windows.h>
#include <shellapi.h>

#include <stdio.h>
#include <string.h>

#include "app/fixture.h"
#include "app/inputs_plan.h"
#include "app/inputs_read.h"
#include "app/points_json.h"
#include "app/product_json.h"
#include "app/scan_json.h"
#include "bacnet/point_read.h"
#include "device/connection.h"
#include "device/read_path.h"
#include "device/registry.h"
#include "discovery/scanner.h"
#include "http/server.h"
#include "json/read.h"
#include "net/interfaces.h"
#include "web/devices_page.h"
#include "web/inputs_page.h"

int run_selftests(int argc, char** argv);

namespace
{
    // Not 80 or 8080: this is a developer tool on a technician's machine and
    // should not squat on a port something else probably wants.
    constexpr unsigned short kPort = 8730;

    // True when this process owns its console alone, which is what happens when
    // it is double-clicked from Explorer rather than run from a shell. In that
    // case the window closes the instant main returns, so an error message that
    // is merely printed is an error message nobody reads.
    //
    // This was not hypothetical: the first version exited immediately when the
    // port was already held by another instance, and the only symptom was a
    // window that flashed and vanished. "It doesn't open" is exactly the kind of
    // unexplained failure this tool exists to stop producing.
    bool owns_console_alone()
    {
        DWORD pids[4] = {};
        return GetConsoleProcessList(pids, 4) <= 1;
    }

    void wait_before_closing()
    {
        if (!owns_console_alone())
            return;

        printf("\nPress Enter to close.");
        (void)getchar();
    }

    t5000::app::DeviceInfo fixture_device()
    {
        t5000::app::DeviceInfo d;
        d.serial_number = 500123;
        d.product_id    = 10;    // PM_TSTAT10
        d.firmware      = 520;   // deliberately below the PTP cutoff
        d.protocol      = 13;    // PROTOCOL_MB_TCPIP_TO_MB_RS485
        d.is_fixture    = true;
        return d;
    }

    t5000::http::Response bad_request(const std::string& message)
    {
        t5000::http::Response r = t5000::http::Response::json(
            "{\"ok\":false,\"message\":\"" + t5000::app::json_escape(message) + "\"}");
        r.status = 400;
        return r;
    }

    // Everything the tool has found, and why the list looks the way it does.
    //
    // One process, one technician, one building. A mutex would be protecting
    // against a concurrency the server does not have - http::Server is
    // single-threaded and blocking, and the scan runs inside a request rather
    // than beside it.
    t5000::device::Registry g_registry;
    t5000::app::ScanSummary g_summary;

    // Advanced by every request sent, across reads, so a late reply to one
    // read cannot be taken for an answer to the next.
    uint8_t g_next_invoke_id = 1;

    // Reads the selected device's inputs off the wire, or says why not.
    //
    // Whether a request is sent at all is decided in app::plan_inputs_read,
    // which is tested; this only carries it out. Synchronous, as the scan is:
    // the server answers one request at a time. A silent device costs two
    // attempts of three seconds before the page is told so.
    std::string read_selected_inputs(const t5000::device::DeviceRecord& d)
    {
        using namespace t5000;

        const app::InputsPlan plan = app::plan_inputs_read(d);
        if (!plan.can_read)
            return app::build_unavailable_inputs_json((int)d.serial_number, d.address_note, plan.reason);

        bacnet::UdpReadTransport transport(plan.endpoint);
        std::string error;
        if (!transport.open(error))
        {
            return app::build_unavailable_inputs_json(
                (int)d.serial_number, d.address_note, "Nothing was sent. " + error);
        }

        const app::InputsPageRead read = app::read_inputs_page(
            transport, plan.endpoint, d.product, d.serial_number, bacnet::ReadSettings(), g_next_invoke_id);
        if (!read.ok)
            return app::build_unavailable_inputs_json((int)d.serial_number, d.address_note, read.error);

        app::DeviceInfo info;
        info.serial_number  = (int)d.serial_number;
        info.product_id     = (int)static_cast<uint8_t>(d.product);
        info.firmware       = d.firmware;
        info.protocol       = app::kProtocolBacnetIp;
        info.read_from_wire = true;
        info.address        = plan.endpoint.text();

        device::Decision decision = plan.decision;
        if (!plan.note.empty())
            decision.detail += " " + plan.note;

        app::InputsPanel panel;
        panel.known    = read.panel.settings_known;
        panel.settings = read.panel.settings;
        panel.product  = d.product;
        panel.ranges   = read.panel.ranges;
        panel.note     = read.panel.note;

        return app::build_inputs_json(info, decision, read.points, panel);
    }
}

int main(int argc, char** argv)
{
    if (argc > 1 && strcmp(argv[1], "--selftest") == 0)
        return run_selftests(argc, argv);

    // For driving the tool from a script. Opening a browser on every start is
    // right for a technician and wrong for a test run: the tab it opens is a
    // live page, one click from a scan that broadcasts on whatever network the
    // machine is on - which has happened during development.
    bool open_a_browser = true;
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--no-browser") == 0)
            open_a_browser = false;
    }

    using namespace t5000;

    http::Server server(kPort);

    // The device list is the front door. Every other screen needs a selected
    // device before it can mean anything, and landing on a grid of fixture
    // points invites the reader to believe it came from somewhere.
    server.route("/", [](const http::Request&) {
        return http::Response::html(web::kDevicesPage);
    });

    server.route("/inputs", [](const http::Request&) {
        return http::Response::html(web::kInputsPage);
    });

    // Loaded once at startup and held in memory. A tool driven by one person at
    // one controller does not need more, and re-reading the file per request
    // would make a hand-edited config take effect halfway through a session.
    static device::Connection connection;
    static const std::string config_path = device::default_config_path();
    {
        std::string load_error;
        if (device::load(config_path, connection, load_error) )
            printf("  config    %s\n", config_path.c_str());
        else if (!load_error.empty())
            printf("  config    %s could not be read: %s\n", config_path.c_str(), load_error.c_str());
        else
            printf("  config    none yet - using defaults\n");
    }

    server.route("/api/connection", [](const http::Request& req) {
        if (req.method == "POST")
        {
            device::Connection incoming = connection;   // start from current, patch
            std::string parse_error;

            if (!device::from_json(req.body, incoming, parse_error))
            {
                http::Response r = http::Response::json(
                    "{\"ok\":false,\"errors\":[{\"field\":\"\",\"message\":\"" +
                    app::json_escape(parse_error) + "\"}]}");
                r.status = 400;
                return r;
            }

            // Refuse to persist something that cannot work. Saving first and
            // failing at connect time is how a settings screen ends up blamed
            // for a wiring problem.
            const auto errors = device::validate(incoming);
            if (!errors.empty())
            {
                std::string body = "{\"ok\":false,\"errors\":[";
                for (size_t i = 0; i < errors.size(); i++)
                {
                    if (i) body += ',';
                    body += "{\"field\":\"" + app::json_escape(errors[i].field) +
                            "\",\"message\":\"" + app::json_escape(errors[i].message) + "\"}";
                }
                body += "]}";

                http::Response r = http::Response::json(body);
                r.status = 400;
                return r;
            }

            std::string save_error;
            if (!device::save(config_path, incoming, save_error))
            {
                http::Response r = http::Response::json(
                    "{\"ok\":false,\"errors\":[{\"field\":\"\",\"message\":\"" +
                    app::json_escape(save_error) + "\"}]}");
                r.status = 500;
                return r;
            }

            connection = incoming;
            return http::Response::json(
                "{\"ok\":true,\"savedTo\":\"" + app::json_escape(config_path) +
                "\",\"connection\":" + device::to_json(connection) + "}");
        }

        // GET returns the settings and how they currently validate, so the form
        // can show existing problems without waiting for a submit.
        const auto errors = device::validate(connection);
        std::string body = "{\"connection\":" + device::to_json(connection) +
                           ",\"configPath\":\"" + app::json_escape(config_path) +
                           "\",\"baudRates\":[";
        const auto& rates = device::supported_baud_rates();
        for (size_t i = 0; i < rates.size(); i++)
        {
            if (i) body += ',';
            body += std::to_string(rates[i]);
        }
        body += "],\"errors\":[";
        for (size_t i = 0; i < errors.size(); i++)
        {
            if (i) body += ',';
            body += "{\"field\":\"" + app::json_escape(errors[i].field) +
                    "\",\"message\":\"" + app::json_escape(errors[i].message) + "\"}";
        }
        body += "]}";
        return http::Response::json(body);
    });

    // Which interfaces a scan could go out of. Enumerated per request rather
    // than cached: a technician plugging into the building network is the
    // normal case, and a list captured at startup would be stale exactly when
    // it matters.
    server.route("/api/interfaces", [](const http::Request&) {
        std::string error;
        const auto interfaces = net::ipv4_interfaces(error);
        return http::Response::json(app::build_interfaces_json(interfaces, error));
    });

    server.route("/api/devices", [](const http::Request&) {
        return http::Response::json(app::build_devices_json(g_registry, g_summary));
    });

    // Runs one scan, synchronously. The request takes as long as the scan
    // does - up to about nine seconds - and the page says so before it starts.
    //
    // Deliberately not on a worker thread. The scanner returns its whole
    // result at the end, so a thread would publish nothing sooner; getting
    // devices to appear as they answer means threading a progress callback
    // through the receive loop, and that loop has already had three bugs in
    // it. A spinner is not worth reopening it.
    server.route("/api/scan", [](const http::Request& req) {
        if (req.method != "POST")
            return bad_request("A scan is started with POST.");

        std::string interface_ip;
        int wait_ms = discovery::ScanSettings().total_timeout_ms;

        if (!req.body.empty())
        {
            if (!json::read_string(req.body, "interfaceIp", interface_ip))
                return bad_request("interfaceIp must be a string.");
            if (!json::read_int(req.body, "waitMs", wait_ms))
                return bad_request("waitMs must be a number.");
        }

        // Clamped rather than rejected. These come from the page's own
        // controls, so an out-of-range value is a bug here rather than
        // something the operator did, and refusing the scan would be a dead
        // end where a sane bound is not.
        if (wait_ms < 1000)  wait_ms = 1000;
        if (wait_ms > 60000) wait_ms = 60000;

        g_summary = app::ScanSummary();
        g_summary.has_scanned  = true;
        g_summary.interface_ip = interface_ip;
        g_summary.waited_ms    = wait_ms;

        discovery::UdpTransport transport(interface_ip);

        std::string open_error;
        if (!transport.open(open_error))
        {
            g_summary.error = open_error;
            return http::Response::json(app::build_devices_json(g_registry, g_summary));
        }

        discovery::ScanSettings settings;
        settings.total_timeout_ms = wait_ms;

        const discovery::ScanResult result = discovery::scan(transport, settings);

        g_summary.stats = result.stats;
        g_summary.error = result.error;

        // Merged, not replaced. A controller that answered earlier and stayed
        // quiet this time is information worth keeping on screen; dropping it
        // would make a flaky device look like one that was never there.
        for (const auto& d : result.devices)
            g_registry.add_or_merge(d);

        // Over the WHOLE list, not just this scan. Two devices sharing an id
        // can answer on different scans and never appear in one result, and a
        // rescan that only one of a pair answers would otherwise leave the
        // other accusing a device the page now shows as clean.
        g_summary.stats.duplicate_modbus_ids =
            g_registry.refresh_duplicate_modbus_ids();

        return http::Response::json(app::build_devices_json(g_registry, g_summary));
    });

    server.route("/api/devices/select", [](const http::Request& req) {
        if (req.method != "POST")
            return bad_request("A selection is made with POST.");

        unsigned long long raw = 0;
        if (!json::read_u64(req.body, "handle", raw))
            return bad_request("handle must be a non-negative whole number.");

        // A handle the page is holding for a device that has since gone
        // resolves to nothing, and that is reported rather than smoothed over.
        // The registry clears the selection on a miss, so the response below
        // already shows "nothing selected" - the page does not have to infer
        // it from an error code.
        const bool found = g_registry.select_by_handle(device::to_handle(raw));

        std::string body = "{\"ok\":";
        body += found ? "true" : "false";
        if (!found)
            body += ",\"message\":\"That device is no longer in the list. "
                    "Scan again to see what is there now.\"";
        body += ",\"state\":" + app::build_devices_json(g_registry, g_summary) + "}";
        return http::Response::json(body);
    });

    server.route("/api/devices/clear", [](const http::Request& req) {
        if (req.method != "POST")
            return bad_request("Clearing the list is done with POST.");

        g_registry.clear();
        g_summary = app::ScanSummary();
        return http::Response::json(app::build_devices_json(g_registry, g_summary));
    });

    // What the tool knows about products. Served so the capability table is
    // inspectable rather than implicit - "this device is not supported" is a
    // much more useful message when the reason is one request away.
    server.route("/api/products", [](const http::Request&) {
        return http::Response::json(app::build_products_json());
    });

    // The selected device, resolved through the product model. Falls back to
    // the fixture only when nothing real is selected.
    server.route("/api/device", [](const http::Request&) {
        if (const device::DeviceRecord* d = g_registry.selected())
            return http::Response::json(
                app::build_product_json((int)static_cast<uint8_t>(d->product), d->mini_type));

        const app::DeviceInfo d = fixture_device();
        return http::Response::json(app::build_product_json(d.product_id, /*mini_type*/ 0));
    });

    server.route("/api/inputs", [](const http::Request&) {
        // A real device is selected: read it, or say why not. Never the
        // fixture - invented points under a named controller's serial look
        // like an answer, which is worse than an empty screen.
        if (const device::DeviceRecord* selected = g_registry.selected())
            return http::Response::json(read_selected_inputs(*selected));

        const app::DeviceInfo device = fixture_device();

        // The fixture is deliberately a Tstat below the PTP firmware cutoff, so
        // the degraded-path banner is exercised every time rather than only
        // being seen for the first time in the field.
        const t5000::device::Decision decision =
            t5000::device::choose_read_path(device.product_id, device.firmware, device.protocol);

        return http::Response::json(
            app::build_inputs_json(device, decision, app::fixture_points()));
    });

    char url[64];
    snprintf(url, sizeof(url), "http://127.0.0.1:%u/", (unsigned)kPort);

    printf("T5000\n");
    printf("  serving   %s\n", url);
    printf("  scanning  read-only - no device is written to\n");
    printf("  points    read-only, from the selected device over BACnet/IP;\n");
    printf("            sample data only when no device is selected\n");
    printf("  bind      loopback only\n\n");
    printf("Ctrl-C to stop.\n");

    // The whole tool is a web page; making the operator find and paste the URL
    // is a step with no purpose. Opened from the ready callback rather than
    // here, so a failed bind does not launch a tab pointing at a URL this
    // process is not serving. Failing to open a browser is not a reason to stop
    // serving, so the result is ignored - the URL is on screen either way.
    const auto open_browser = [&url, open_a_browser]() {
        if (open_a_browser)
            ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
    };

    if (!server.serve_forever(open_browser))
    {
        fprintf(stderr, "\nCould not start: %s\n", server.last_error().c_str());

        // Name the most likely cause rather than only the errno-level one. The
        // usual reason this port is taken is another copy of this same tool.
        if (server.last_error().find("bind") != std::string::npos)
        {
            fprintf(stderr,
                    "\nAnother copy of T5000 is probably already running and\n"
                    "holding port %u. Close it, or check with:\n"
                    "    Get-NetTCPConnection -LocalPort %u -State Listen\n",
                    (unsigned)kPort, (unsigned)kPort);
        }

        wait_before_closing();
        return 1;
    }
    return 0;
}
