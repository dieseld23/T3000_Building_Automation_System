#include "server.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <stdio.h>

#pragma comment(lib, "Ws2_32.lib")

namespace t3000::http
{
    namespace
    {
        const char* reason_phrase(int status)
        {
            switch (status)
            {
            case 200: return "OK";
            case 400: return "Bad Request";
            case 404: return "Not Found";
            case 500: return "Internal Server Error";
            default:  return "OK";
            }
        }

        // Reads until the end of the headers. The bodies this server accepts are
        // small, and it does not accept uploads at all, so there is no need to
        // stream or to handle chunked encoding.
        bool read_request(SOCKET client, std::string& out)
        {
            char buffer[4096];
            out.clear();

            while (out.find("\r\n\r\n") == std::string::npos)
            {
                const int n = recv(client, buffer, (int)sizeof(buffer), 0);
                if (n <= 0)
                    return false;

                out.append(buffer, (size_t)n);

                // A request head this large is not something a browser sends.
                if (out.size() > 64 * 1024)
                    return false;
            }
            return true;
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
            return true;
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

            if (!read_request(client, raw) || !parse_request(raw, req))
            {
                res.status = 400;
                res.body   = "Bad request";
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
