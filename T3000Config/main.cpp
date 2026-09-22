// T3000Config - a standalone configuration tool for Tstat/T3 units.
//
//   T3000Config.exe --selftest    run the self-tests, exit non-zero on failure
//   T3000Config.exe               serve the UI on http://127.0.0.1:8730
//
// It does not talk to a device yet. Serving the fixture is how the page and the
// JSON contract get built and reviewed before any of it is pointed at live
// equipment, and everything it serves is flagged as sample data.

#include <windows.h>
#include <shellapi.h>

#include <stdio.h>
#include <string.h>

#include "app/fixture.h"
#include "app/points_json.h"
#include "device/connection.h"
#include "device/read_path.h"
#include "http/server.h"
#include "web/inputs_page.h"

int run_selftests();

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

    t3000::app::DeviceInfo fixture_device()
    {
        t3000::app::DeviceInfo d;
        d.serial_number = 500123;
        d.product_id    = 10;    // PM_TSTAT10
        d.firmware      = 520;   // deliberately below the PTP cutoff
        d.protocol      = 13;    // PROTOCOL_MB_TCPIP_TO_MB_RS485
        d.is_fixture    = true;
        return d;
    }
}

int main(int argc, char** argv)
{
    if (argc > 1 && strcmp(argv[1], "--selftest") == 0)
        return run_selftests();

    using namespace t3000;

    http::Server server(kPort);

    server.route("/", [](const http::Request&) {
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

    server.route("/api/inputs", [](const http::Request&) {
        const app::DeviceInfo device = fixture_device();

        // The fixture is deliberately a Tstat below the PTP firmware cutoff, so
        // the degraded-path banner is exercised every time rather than only
        // being seen for the first time in the field.
        const device::Decision decision =
            device::choose_read_path(device.product_id, device.firmware, device.protocol);

        return http::Response::json(
            app::build_inputs_json(device, decision, app::fixture_points()));
    });

    char url[64];
    snprintf(url, sizeof(url), "http://127.0.0.1:%u/", (unsigned)kPort);

    printf("T3000Config\n");
    printf("  serving   %s\n", url);
    printf("  data      FIXTURE - no device is connected\n");
    printf("  bind      loopback only\n\n");
    printf("Ctrl-C to stop.\n");

    // The whole tool is a web page; making the operator find and paste the URL
    // is a step with no purpose. Opened from the ready callback rather than
    // here, so a failed bind does not launch a tab pointing at a URL this
    // process is not serving. Failing to open a browser is not a reason to stop
    // serving, so the result is ignored - the URL is on screen either way.
    const auto open_browser = [&url]() {
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
                    "\nAnother copy of T3000Config is probably already running and\n"
                    "holding port %u. Close it, or check with:\n"
                    "    Get-NetTCPConnection -LocalPort %u -State Listen\n",
                    (unsigned)kPort, (unsigned)kPort);
        }

        wait_before_closing();
        return 1;
    }
    return 0;
}
