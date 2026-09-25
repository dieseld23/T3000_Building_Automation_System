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
            "X-Not-Host: evil.example\r\n"
            "hOsT:   127.0.0.1:8730  \r\n"
            "Origin: http://127.0.0.1:8730\r\n"
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
}

int run_http_server_tests()
{
    test_headers_are_read_by_name();
    test_only_this_tool_may_ask();
    return 0;
}
