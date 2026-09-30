#pragma once

// A deliberately small HTTP server.
//
// It binds 127.0.0.1 and nothing else. This tool writes to live building
// equipment, so reaching it from another machine is a decision to be made
// explicitly, with authentication designed for it - not something that falls out
// of a default bind address. The tablet and remote-access story comes later and
// on purpose.
//
// Single-threaded and blocking. One technician configuring one controller does
// not need more, and the device layer underneath serialises anyway. The cost is
// that a connection being read holds up every other, so each request has to
// arrive within kRequestBudgetMs of its connection being accepted; a client
// that stalls, or connects and sends nothing, is dropped then.
//
// No framework: a dependency here would be the largest thing in the project, and
// what is needed is a few hundred lines of request line, headers, and a body.

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace t5000::http
{
    struct Request
    {
        std::string method;
        std::string path;      // no query string; see `query`
        std::string query;     // raw, undecoded
        std::string body;      // empty unless the request carried one

        // Empty when the request did not carry the header. See
        // from_this_tool below for what they are for.
        std::string host;
        std::string origin;

        // Its Content-Length header, matched on the whole name like the two
        // above: 0 when it carried none. A length past what a size_t holds
        // is the most it holds, which body_limit then refuses.
        size_t content_length = 0;
    };

    // The request line, the Host, Origin and Content-Length headers, and
    // the body. False for a Content-Length that is not a plain decimal
    // number, or is given twice: read one way here and another by whatever
    // sent it, it would split the body in the wrong place.
    bool parse_request(const std::string& raw, Request& req);

    // Whether a request came from this tool's own page, or from a script
    // with no page at all, rather than from some other web page open in the
    // same browser.
    //
    // Binding to loopback keeps other machines out and does nothing about
    // that: any page the technician has open can send a POST to
    // 127.0.0.1:8730, and "forget every device" is one. So:
    //
    //   - Host must name this server, as 127.0.0.1 or localhost on its port.
    //     A page on a hostile domain that has been re-pointed at 127.0.0.1
    //     (DNS rebinding) still sends its own name here.
    //   - Origin, when there is one, must be this server's. A browser sends it
    //     on every cross-site request that could change something. A script
    //     such as Invoke-WebRequest sends none, and is let through.
    //
    // False with the reason in `why`, and the server answers 403.
    bool from_this_tool(const Request& req, unsigned short port, std::string& why);

    // How long one request may take to arrive, head and body together,
    // counted from when its connection is accepted. The server reads one
    // connection at a time, so a client that stops part way through holds
    // up every other request, and this is for how long at most. Several
    // such clients cost this much each.
    inline constexpr unsigned kRequestBudgetMs = 5000;

    // What is left of a request's budget `elapsed_ms` after its connection
    // was accepted: at least 1 while any is left, and 0 once it is spent.
    // The server sets SO_RCVTIMEO to this before each recv, so the budget
    // bounds the whole request rather than each recv. SO_RCVTIMEO takes 0
    // to mean no limit, so a spent budget is never set: it ends the read.
    unsigned long budget_left_ms(unsigned long long elapsed_ms, unsigned budget_ms);

    struct Response
    {
        int         status = 200;
        std::string content_type = "text/plain; charset=utf-8";
        std::string body;

        static Response json(std::string body);
        static Response html(std::string body);
        static Response not_found();
    };

    using Handler = std::function<Response(const Request&)>;

    // The most body a request may carry: a small JSON object is all a page
    // sends. A route that takes more says so when it is added.
    inline constexpr size_t kMaxBody = 256 * 1024;

    struct Route
    {
        std::string path;
        Handler     handler;
        size_t      max_body = kMaxBody;   // for a POST; see body_limit
    };

    // How much body a request may carry, from its head alone, before any
    // of the body is read: the route's own max_body for a POST to exactly
    // its path from T5000's own page or a script (from_this_tool), and
    // kMaxBody for anything else. So a page that is not T5000's cannot make
    // it take in more than kMaxBody, whatever it names.
    size_t body_limit(const Request& head, unsigned short port, const std::vector<Route>& routes);

    // Whether a request whose head declares `declared` bytes of body is
    // refused them by body_limit. If so, `refused` is the answer, a 413
    // saying how much it carries and how much it may, and the server reads
    // none of the body.
    bool body_refused(const Request& head, size_t declared, unsigned short port, const std::vector<Route>& routes,
                      Response& refused);

    class Server
    {
    public:
        explicit Server(unsigned short port);
        ~Server();

        Server(const Server&) = delete;
        Server& operator=(const Server&) = delete;

        // `max_body` above kMaxBody lets a POST to this path carry that
        // much (body_limit).
        void route(const std::string& path, Handler handler, size_t max_body = kMaxBody);

        // Blocks. Returns false if the listening socket could not be opened,
        // with the reason in last_error().
        //
        // on_ready, if given, runs once - after the socket is listening and
        // before the first accept. Anything that should happen only on a
        // successful start belongs here rather than before the call: opening a
        // browser beforehand means a failed bind still launches a tab, pointing
        // at a URL this process is not serving.
        bool serve_forever(const std::function<void()>& on_ready = nullptr);

        // One connection, start to finish: its request read within
        // `budget_ms`, checked, routed and answered, and the connection
        // closed. serve_forever calls this for each connection it accepts;
        // the self-test calls it directly, over a socket on loopback.
        // `client` is the accepted SOCKET, kept opaque to the header as the
        // listener is.
        void answer(uintptr_t client, unsigned budget_ms = kRequestBudgetMs);

        const std::string& last_error() const { return m_error; }
        unsigned short port() const { return m_port; }

    private:
        unsigned short m_port;
        std::string    m_error;
        void*          m_listen = nullptr;   // SOCKET, kept opaque to the header
        std::vector<Route> m_routes;
    };
}
