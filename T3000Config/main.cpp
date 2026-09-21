// T3000Config - a standalone configuration tool for Tstat/T3 units.
//
//   T3000Config.exe --selftest    run the self-tests, exit non-zero on failure
//   T3000Config.exe               serve the UI on http://127.0.0.1:8730
//
// It does not talk to a device yet. Serving the fixture is how the page and the
// JSON contract get built and reviewed before any of it is pointed at live
// equipment, and everything it serves is flagged as sample data.

#include <stdio.h>
#include <string.h>

#include "app/fixture.h"
#include "app/points_json.h"
#include "device/read_path.h"
#include "http/server.h"
#include "web/inputs_page.h"

int run_selftests();

namespace
{
    // Not 80 or 8080: this is a developer tool on a technician's machine and
    // should not squat on a port something else probably wants.
    constexpr unsigned short kPort = 8730;

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

    printf("T3000Config\n");
    printf("  serving   http://127.0.0.1:%u/\n", (unsigned)kPort);
    printf("  data      FIXTURE - no device is connected\n");
    printf("  bind      loopback only\n\n");
    printf("Ctrl-C to stop.\n");

    if (!server.serve_forever())
    {
        fprintf(stderr, "\nCould not start: %s\n", server.last_error().c_str());
        return 1;
    }
    return 0;
}
