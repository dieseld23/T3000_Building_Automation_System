// Tests for the HTTP server's request parsing and its check that a request
// came from T5000's own page.
//
// The check matters because the server is on loopback, and loopback keeps
// out other machines but not other web pages: any page open in the
// technician's browser can POST to 127.0.0.1:8730.

#include "server.h"
#include "../testing/check.h"

namespace
{
    using namespace t5000::http;
    using namespace t5000::testing;

    Request parsed(const std::string& raw)
    {
        Request r;
        check(parse_request(raw, r), "the request parses");
        return r;
    }

    void test_headers_are_read_by_name()
    {
        section("Host and Origin are read by their whole name, in any case");

        const Request r = parsed(
            "POST /api/devices/forget HTTP/1.1\r\n"
            "hOsT:   127.0.0.1:8730  \r\n"
            "Origin: http://127.0.0.1:8730\r\n"
            "X-Not-Host: evil.example\r\n"
            "X-Forwarded-Origin: https://evil.example\r\n"
            "Content-Length: 15\r\n"
            "\r\n"
            "{\"handle\":\"12\"}");

        check(r.method == "POST", "the method");
        check(r.path == "/api/devices/forget", "the path");
        check(r.host == "127.0.0.1:8730", "the host, trimmed, and not a header that ends in host");
        check(r.origin == "http://127.0.0.1:8730", "the origin");
        check(r.body == "{\"handle\":\"12\"}", "and the body");

        const Request bare = parsed("GET / HTTP/1.1\r\nHost: localhost:8730\r\n\r\n");
        check(bare.origin.empty(), "a request with no Origin has none");
    }

    void test_only_this_tool_may_ask()
    {
        section("only T5000's own page, or a script with no page, is answered");

        std::string why;
        Request r;

        r.host = "127.0.0.1:8730";
        check(from_this_tool(r, 8730, why), "a script, with no Origin");

        r.origin = "http://127.0.0.1:8730";
        check(from_this_tool(r, 8730, why), "T5000's own page");

        r.host   = "LOCALHOST:8730";
        r.origin = "http://localhost:8730";
        check(from_this_tool(r, 8730, why), "the same page reached as localhost");

        r.host   = "127.0.0.1:8730";
        r.origin = "https://evil.example";
        check(!from_this_tool(r, 8730, why), "another site's page is refused");
        check(why.find("evil.example") != std::string::npos, "and the reason names it");

        r.origin = "null";
        check(!from_this_tool(r, 8730, why), "a sandboxed page or a local file is refused");

        r.origin = "http://127.0.0.1:9999";
        check(!from_this_tool(r, 8730, why), "a page on another port of this machine is refused");

        r.origin.clear();
        r.host = "evil.example:8730";
        check(!from_this_tool(r, 8730, why), "a hostile name pointed at 127.0.0.1 is refused");

        r.host.clear();
        check(!from_this_tool(r, 8730, why), "a request naming no host is refused");

        r.host = "127.0.0.1:87300";
        check(!from_this_tool(r, 8730, why), "a port that only starts with ours is refused");
    }

    void test_only_a_route_that_asks_takes_more()
    {
        section("a request may carry more body only for a route that asks, from T5000's own page");

        std::vector<Route> routes(2);
        routes[0].path = "/api/devices";
        routes[1].path     = "/api/firmware/check";
        routes[1].max_body = 16u * 1024 * 1024;

        const auto head = [](const std::string& line, const std::string& origin) {
            std::string raw = line + "\r\nHost: 127.0.0.1:8730\r\n";
            if (!origin.empty())
                raw += "Origin: " + origin + "\r\n";
            return parsed(raw + "Content-Length: 9999999\r\n\r\n");
        };
        const std::string own = "http://127.0.0.1:8730";

        check_eq((long)kMaxBody, 256L * 1024, "everything else: 256 KB");
        check_eq((long)body_limit(head("POST /api/firmware/check?handle=3&name=a.hex HTTP/1.1", own), 8730, routes),
                 16L * 1024 * 1024, "a POST from T5000's page to the route that asks: its own limit");
        check_eq((long)body_limit(head("POST /api/firmware/check HTTP/1.1", ""), 8730, routes),
                 16L * 1024 * 1024, "  and from a script with no page");
        check_eq((long)body_limit(head("GET /api/firmware/check HTTP/1.1", own), 8730, routes), (long)kMaxBody,
                 "a GET: 256 KB");
        check_eq((long)body_limit(head("POST /api/devices HTTP/1.1", own), 8730, routes), (long)kMaxBody,
                 "a route that does not ask: 256 KB");
        check_eq((long)body_limit(head("POST /api/firmware/checks HTTP/1.1", own), 8730, routes), (long)kMaxBody,
                 "a path that only starts with it: 256 KB");
        check_eq((long)body_limit(head("POST /api/firmware/check HTTP/1.1", "https://evil.example"), 8730, routes),
                 (long)kMaxBody, "another site's page: 256 KB, before any of the body is read");

        Request renamed = head("POST /api/firmware/check HTTP/1.1", own);
        renamed.host = "evil.example:8730";
        check_eq((long)body_limit(renamed, 8730, routes), (long)kMaxBody, "a hostile name pointed here: 256 KB");

        std::vector<Route> small(1);
        small[0].path     = "/api/firmware/check";
        small[0].max_body = 10;
        check_eq((long)body_limit(head("POST /api/firmware/check HTTP/1.1", own), 8730, small), (long)kMaxBody,
                 "a route asking for less than 256 KB still gets 256 KB");

        const size_t big = 16u * 1024 * 1024;
        Response refused;
        check(!body_refused(head("POST /api/firmware/check HTTP/1.1", own), big, 8730, routes, refused),
              "16 MiB to the route that asks, from T5000's page: read");
        check(body_refused(head("POST /api/firmware/check HTTP/1.1", own), big + 1, 8730, routes, refused),
              "  a byte more: refused");
        check_eq(refused.status, 413, "  with 413");
        check(refused.body == "The request carries 16777217 bytes; it may carry 16777216.",
              "  saying how much it carries and how much it may");
        check(!body_refused(head("POST /api/devices HTTP/1.1", own), kMaxBody, 8730, routes, refused),
              "256 KB to any other route: read");
        check(body_refused(head("POST /api/devices HTTP/1.1", own), kMaxBody + 1, 8730, routes, refused),
              "  a byte more: refused");
        check(body_refused(head("POST /api/firmware/check HTTP/1.1", "https://evil.example"), kMaxBody + 1, 8730,
                           routes, refused) &&
                  refused.body == "The request carries 262145 bytes; it may carry 262144.",
              "another site's page, a byte over 256 KB to the route that asks: refused");
        check(!body_refused(head("GET /api/firmware/check HTTP/1.1", own), 0, 8730, routes, refused),
              "no body at all: read");
    }
}

int run_http_server_tests()
{
    test_headers_are_read_by_name();
    test_only_this_tool_may_ask();
    test_only_a_route_that_asks_takes_more();
    return 0;
}
