#include "server.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <ctype.h>
#include <stdio.h>

#pragma comment(lib, "Ws2_32.lib")

namespace t5000::http
{
    namespace
    {
        const char* reason_phrase(int status)
        {
            switch (status)
            {
            case 200: return "OK";
            case 400: return "Bad Request";
            case 403: return "Forbidden";
            case 404: return "Not Found";
            case 500: return "Internal Server Error";
            default:  return "OK";
            }
        }

        size_t header_value_size(const std::string& head, const char* name)
        {
            // Header names are case-insensitive, and browsers do not agree on
            // the casing of Content-Length, so compare lowercased.
            std::string lowered;
            lowered.reserve(head.size());
            for (char c : head)
                lowered += (char)tolower((unsigned char)c);

            const size_t at = lowered.find(name);
            if (at == std::string::npos)
                return 0;

            const size_t colon = lowered.find(':', at);
            if (colon == std::string::npos)
                return 0;

            return (size_t)strtoul(head.c_str() + colon + 1, nullptr, 10);
        }

        // Reads the head, then whatever body Content-Length declares. Chunked
        // encoding is not handled: nothing here asks for it, and fetch() with a
        // string body always sends a length.
        bool read_request(SOCKET client, std::string& out)
        {
            char buffer[4096];
            out.clear();

            size_t head_end = std::string::npos;
            while ((head_end = out.find("\r\n\r\n")) == std::string::npos)
            {
                const int n = recv(client, buffer, (int)sizeof(buffer), 0);
                if (n <= 0)
                    return false;

                out.append(buffer, (size_t)n);

                // A request head this large is not something a browser sends.
                if (out.size() > 64 * 1024)
                    return false;
            }

            const size_t body_start = head_end + 4;
            const size_t declared   = header_value_size(out.substr(0, head_end), "content-length");

            // Bounded deliberately. This accepts a small JSON settings object
            // and nothing else, so a large declared length is a reason to stop
            // rather than something to allocate for.
            if (declared > 256 * 1024)
                return false;

            while (out.size() - body_start < declared)
            {
                const int n = recv(client, buffer, (int)sizeof(buffer), 0);
                if (n <= 0)
                    return false;
                out.append(buffer, (size_t)n);
            }
            return true;
        }

        std::string lowercase(std::string s)
        {
            for (char& c : s)
                c = (char)tolower((unsigned char)c);
            return s;
        }

        std::string trimmed(const std::string& s)
        {
            const size_t first = s.find_first_not_of(" \t");
            if (first == std::string::npos)
                return std::string();
            return s.substr(first, s.find_last_not_of(" \t") - first + 1);
        }

        void send_all(SOCKET client, const std::string& data)
        {
            size_t sent = 0;
            while (sent < data.size())
            {
                const int n = send(client, data.data() + sent,
                                   (int)(data.size() - sent), 0);
                if (n <= 0)
                    return;
                sent += (size_t)n;
            }
        }
    }

    bool parse_request(const std::string& raw, Request& req)
    {
        const size_t line_end = raw.find("\r\n");
        if (line_end == std::string::npos)
            return false;

        const std::string line = raw.substr(0, line_end);

        const size_t sp1 = line.find(' ');
        if (sp1 == std::string::npos) return false;
        const size_t sp2 = line.find(' ', sp1 + 1);
        if (sp2 == std::string::npos) return false;

        req.method = line.substr(0, sp1);

        std::string target = line.substr(sp1 + 1, sp2 - sp1 - 1);
        const size_t q = target.find('?');
        if (q == std::string::npos)
        {
            req.path  = target;
            req.query.clear();
        }
        else
        {
            req.path  = target.substr(0, q);
            req.query = target.substr(q + 1);
        }

        const size_t head_end = raw.find("\r\n\r\n");
        const size_t head_stop = head_end == std::string::npos ? raw.size() : head_end;

        // Header lines, one at a time, matched on the whole name. Only the
        // two from_this_tool needs are kept.
        req.host.clear();
        req.origin.clear();
        size_t at = line_end + 2;
        while (at < head_stop)
        {
            size_t end = raw.find("\r\n", at);
            if (end == std::string::npos || end > head_stop)
                end = head_stop;

            const std::string header = raw.substr(at, end - at);
            const size_t colon = header.find(':');
            if (colon != std::string::npos)
            {
                const std::string name  = lowercase(trimmed(header.substr(0, colon)));
                const std::string value = trimmed(header.substr(colon + 1));
                if (name == "host")
                    req.host = value;
                else if (name == "origin")
                    req.origin = value;
            }
            at = end + 2;
        }

        if (head_end != std::string::npos)
            req.body = raw.substr(head_end + 4);

        return true;
    }

    bool from_this_tool(const Request& req, unsigned short port, std::string& why)
    {
        const std::string p = ":" + std::to_string(port);
        const std::string host = lowercase(req.host);
        if (host != "127.0.0.1" + p && host != "localhost" + p)
        {
            why = req.host.empty() ? "The request names no host."
                                   : "The request is addressed to " + req.host + ", not to T5000.";
            return false;
        }

        if (!req.origin.empty())
        {
            const std::string origin = lowercase(req.origin);
            if (origin != "http://127.0.0.1" + p && origin != "http://localhost" + p)
            {
                why = "The request came from a page at " + req.origin +
                      ". Only T5000's own pages may use it.";
                return false;
            }
        }
        return true;
    }

    Response Response::json(std::string body)
    {
        Response r;
        r.content_type = "application/json; charset=utf-8";
        r.body = std::move(body);
        return r;
    }

    Response Response::html(std::string body)
    {
        Response r;
        r.content_type = "text/html; charset=utf-8";
        r.body = std::move(body);
        return r;
    }

    Response Response::not_found()
    {
        Response r;
        r.status = 404;
        r.body = "Not found";
        return r;
    }

    Server::Server(unsigned short port) : m_port(port) {}

    Server::~Server()
    {
        if (m_listen != nullptr)
            closesocket((SOCKET)(uintptr_t)m_listen);
        WSACleanup();
    }

    void Server::route(const std::string& path, Handler handler)
    {
        m_routes.emplace_back(path, std::move(handler));
    }

    bool Server::serve_forever(const std::function<void()>& on_ready)
    {
        WSADATA wsa{};
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        {
            m_error = "WSAStartup failed";
            return false;
        }

        SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener == INVALID_SOCKET)
        {
            m_error = "could not create a socket";
            return false;
        }
        m_listen = (void*)(uintptr_t)listener;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port   = htons(m_port);

        // Loopback explicitly, not INADDR_ANY. See the header: this is the whole
        // security posture of the tool right now, and it is one line, so it
        // should be an obvious one.
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        if (bind(listener, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
        {
            char msg[128];
            snprintf(msg, sizeof(msg),
                     "could not bind 127.0.0.1:%u (error %d) - is it already in use?",
                     (unsigned)m_port, WSAGetLastError());
            m_error = msg;
            return false;
        }

        if (listen(listener, SOMAXCONN) == SOCKET_ERROR)
        {
            m_error = "listen failed";
            return false;
        }

        // Listening for real now, so anything conditional on a successful start
        // can run. Before this point the bind may still fail.
        if (on_ready)
            on_ready();

        for (;;)
        {
            SOCKET client = accept(listener, nullptr, nullptr);
            if (client == INVALID_SOCKET)
                continue;

            std::string raw;
            Request req;
            Response res;

            std::string why;
            if (!read_request(client, raw) || !parse_request(raw, req))
            {
                res.status = 400;
                res.body   = "Bad request";
            }
            else if (!from_this_tool(req, m_port, why))
            {
                // Before any route runs, so no handler can forget to check.
                res.status = 403;
                res.body   = why;
            }
            else
            {
                res = Response::not_found();
                for (const auto& r : m_routes)
                {
                    if (r.first == req.path)
                    {
                        res = r.second(req);
                        break;
                    }
                }
            }

            char head[512];
            const int head_len = snprintf(head, sizeof(head),
                "HTTP/1.1 %d %s\r\n"
                "Content-Type: %s\r\n"
                "Content-Length: %zu\r\n"
                "Cache-Control: no-store\r\n"
                "Connection: close\r\n"
                "\r\n",
                res.status, reason_phrase(res.status),
                res.content_type.c_str(), res.body.size());

            send_all(client, std::string(head, (size_t)head_len));
            if (req.method != "HEAD")
                send_all(client, res.body);

            shutdown(client, SD_SEND);
            closesocket(client);
        }
    }
}
