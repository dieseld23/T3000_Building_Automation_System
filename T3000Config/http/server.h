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
// not need more, and the device layer underneath serialises anyway.
//
// No framework: a dependency here would be the largest thing in the project, and
// what is needed is a few hundred lines of request line, headers, and a body.

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace t3000::http
{
    struct Request
    {
        std::string method;
        std::string path;      // no query string; see `query`
        std::string query;     // raw, undecoded
    };

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

    class Server
    {
    public:
        explicit Server(unsigned short port);
        ~Server();

        Server(const Server&) = delete;
        Server& operator=(const Server&) = delete;

        void route(const std::string& path, Handler handler);

        // Blocks. Returns false if the listening socket could not be opened,
        // with the reason in last_error().
        bool serve_forever();

        const std::string& last_error() const { return m_error; }
        unsigned short port() const { return m_port; }

    private:
        unsigned short m_port;
        std::string    m_error;
        void*          m_listen = nullptr;   // SOCKET, kept opaque to the header
        std::vector<std::pair<std::string, Handler>> m_routes;
    };
}
