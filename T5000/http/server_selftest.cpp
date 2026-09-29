// Tests for the HTTP server's request parsing, its check that a request
// came from T5000's own page, and how long it waits for a request to arrive.
//
// The check matters because the server is on loopback, and loopback keeps
// out other machines but not other web pages: any page open in the
// technician's browser can POST to 127.0.0.1:8730.
//
// The wait matters because the server reads one connection at a time: a
// client that stops part way through a request holds up every page and scan
// behind it. That is tested over real sockets on loopback, through
// Server::answer, the part of the server that handles one connection.

#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <stdio.h>
#include <string>
#include <thread>

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

    void test_budget_left()
    {
        section("what is left of a request's time to arrive");

        check_eq((long)budget_left_ms(0, 5000), 5000, "all of it when the connection is accepted");
        check_eq((long)budget_left_ms(4999, 5000), 1, "1 ms just before the end: never 0 while any is left");
        check_eq((long)budget_left_ms(5000, 5000), 0, "none at the end");
        check_eq((long)budget_left_ms(5001, 5000), 0, "and none after it, rather than a wrap to a very long time");
        check_eq((long)budget_left_ms(~0ull, 5000), 0, "however long after");
        check_eq((long)budget_left_ms(0, 0), 0, "and none of a budget of 0");
        check(kRequestBudgetMs >= 1000 && kRequestBudgetMs <= 10000,
              "the budget is a few seconds: ample for any request over loopback, and short for a stalled one");
    }

    // ------------------------------------------------------------- loopback

    using Clock = std::chrono::steady_clock;

    long long ms_since(Clock::time_point t)
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t).count();
    }

    // One connection: how long Server::answer took over it, and all that
    // came back before the server closed it. `ms` is -1 if no connection
    // could be made.
    struct Exchange
    {
        long long   ms = -1;
        std::string reply;
    };

    // The client's end of a connection, run in a thread as a browser or a
    // script would be. `done` turns true when Server::answer has returned.
    // It must stop by itself within a few seconds whatever happens, so that
    // a server that waits too long fails a check rather than hanging the
    // build.
    using ClientEnd = std::function<void(SOCKET, const std::atomic<bool>& done)>;

    // Opens a connection on loopback, and runs `client` at one end and
    // Server::answer, with `budget_ms`, at the other.
    Exchange exchange(Server& server, unsigned budget_ms, const ClientEnd& client)
    {
        Exchange e;

        SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in at = {};
        at.sin_family      = AF_INET;
        at.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        int at_len = sizeof(at);
        if (::bind(listener, (sockaddr*)&at, sizeof(at)) != 0 || ::listen(listener, 1) != 0 ||
            ::getsockname(listener, (sockaddr*)&at, &at_len) != 0)
        {
            ::closesocket(listener);
            return e;
        }

        SOCKET client_end = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        const bool connected = ::connect(client_end, (sockaddr*)&at, sizeof(at)) == 0;
        SOCKET server_end = connected ? ::accept(listener, nullptr, nullptr) : INVALID_SOCKET;
        ::closesocket(listener);
        if (server_end == INVALID_SOCKET)
        {
            ::closesocket(client_end);
            return e;
        }

        std::atomic<bool> done{ false };
        std::thread browser([&]() { client(client_end, done); });

        const Clock::time_point started = Clock::now();
        server.answer((uintptr_t)server_end, budget_ms);
        e.ms = ms_since(started);
        done = true;
        browser.join();

        // The server has closed its end, so this ends. The timeout is only
        // so that a server that has not cannot hang the build.
        const DWORD wait_ms = 2000;
        ::setsockopt(client_end, SOL_SOCKET, SO_RCVTIMEO, (const char*)&wait_ms, (int)sizeof(wait_ms));
        char buffer[4096];
        int n;
        while ((n = ::recv(client_end, buffer, (int)sizeof(buffer), 0)) > 0)
            e.reply.append(buffer, (size_t)n);
        ::closesocket(client_end);
        return e;
    }

    void send_text(SOCKET s, const std::string& text)
    {
        ::send(s, text.data(), (int)text.size(), 0);
    }

    // Keeps the connection open until Server::answer returns, or `ms` pass.
    void hold(const std::atomic<bool>& done, long long ms)
    {
        const Clock::time_point started = Clock::now();
        while (!done && ms_since(started) < ms)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    void check_took(const Exchange& e, long long at_least, long long under, const char* what)
    {
        const bool ok = e.ms >= at_least && e.ms < under;
        check(ok, what);
        if (!ok)
            printf("        took %lld ms; expected at least %lld and under %lld\n", e.ms, at_least, under);
    }

    bool answered(const Exchange& e, const char* status)
    {
        return e.reply.rfind(std::string("HTTP/1.1 ") + status, 0) == 0;
    }

    void test_a_request_has_a_time_to_arrive()
    {
        section("a request that stops part way costs the server its budget, and no more");

        // Balanced by the one WSACleanup in Server's destructor.
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);

        Server server(8730);
        server.route("/", [](const Request&) { return Response::html("<p>T5000</p>"); });
        server.route("/echo", [](const Request& r) { return Response::json(r.body); });

        const std::string own = "Host: 127.0.0.1:8730\r\nOrigin: http://127.0.0.1:8730\r\n";
        const unsigned budget = 300;

        // For a slow machine: each failure below is out by seconds.
        const long long slack = 1500;

        Exchange e = exchange(server, budget, [&](SOCKET s, const std::atomic<bool>&) {
            send_text(s, "POST /echo HTTP/1.1\r\n");
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            send_text(s, own + "Content-Length: 9\r\n\r\n{\"a\"");
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            send_text(s, ":\"b\"}");
        });
        check(answered(e, "200") && e.reply.find("\r\n\r\n{\"a\":\"b\"}") != std::string::npos,
              "a request that arrives in pieces within its budget is read whole and answered");

        e = exchange(server, budget, [&](SOCKET s, const std::atomic<bool>& done) {
            send_text(s, "GET / HTTP/1.1\r\nHost: 127.0.0.1:8730\r\n");
            hold(done, 3000);
        });
        check_took(e, budget - 50, budget + slack, "a head that stops part way is given up on when the budget is spent");
        check(answered(e, "400"), "  and answered 400");

        e = exchange(server, budget, [&](SOCKET, const std::atomic<bool>& done) { hold(done, 3000); });
        check_took(e, budget - 50, budget + slack, "a connection that sends nothing is dropped when the budget is spent");
        check(e.reply.empty(), "  without an answer: nothing was asked");

        e = exchange(server, budget, [&](SOCKET s, const std::atomic<bool>& done) {
            send_text(s, "POST /echo HTTP/1.1\r\n" + own + "Content-Length: 10\r\n\r\nabc");
            hold(done, 3000);
        });
        check_took(e, budget - 50, budget + slack,
                   "a body shorter than its Content-Length is given up on when the budget is spent");
        check(answered(e, "400"), "  and answered 400, not handed to the route");

        e = exchange(server, budget, [&](SOCKET s, const std::atomic<bool>& done) {
            send_text(s, "GET / HTTP/1.1\r\nX-Slow: ");
            const Clock::time_point started = Clock::now();
            while (!done && ms_since(started) < 4000)
            {
                send_text(s, "a");
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        });
        check_took(e, budget - 50, budget + slack,
                   "a client sending a byte at a time gets the same budget, not a fresh one with each byte");

        e = exchange(server, 1000, [&](SOCKET s, const std::atomic<bool>& done) {
            send_text(s, "POST /echo HTTP/1.1\r\n");
            std::this_thread::sleep_for(std::chrono::milliseconds(700));
            send_text(s, own + "Content-Length: 10\r\n\r\nabc");
            hold(done, 4000);
        });
        check_took(e, 950, 1600, "the body gets what the head left of the budget, not a budget of its own");

        e = exchange(server, 3000, [&](SOCKET s, const std::atomic<bool>& done) {
            send_text(s, "POST /echo HTTP/1.1\r\nHost: 127.0.0.1:8730\r\nOrigin: https://evil.example\r\n"
                         "Content-Length: 100\r\n\r\n");
            hold(done, 5000);
        });
        check_took(e, 0, 1500, "another site's page is refused on its head, without waiting for the body it declares");
        check(answered(e, "403"), "  with a 403");

        e = exchange(server, 3000, [&](SOCKET s, const std::atomic<bool>& done) {
            send_text(s, "POST /echo HTTP/1.1\r\n" + own + "Content-Length: 300000\r\n\r\n");
            hold(done, 5000);
        });
        check_took(e, 0, 1500, "a body declared larger than 256 KB is refused on the head, before any of it is read");
        check(answered(e, "400"), "  with a 400");

        e = exchange(server, 0, [&](SOCKET s, const std::atomic<bool>& done) {
            send_text(s, "GET / HTTP/1.1\r\n");
            hold(done, 3000);
        });
        check_took(e, 0, 1500, "a spent budget ends the read, rather than being set as SO_RCVTIMEO's 0 for no limit");
    }
}

int run_http_server_tests()
{
    test_headers_are_read_by_name();
    test_only_this_tool_may_ask();
    test_budget_left();
    test_a_request_has_a_time_to_arrive();
    return 0;
}
