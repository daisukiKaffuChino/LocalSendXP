#ifndef LSXP_HTTPSERVER_H
#define LSXP_HTTPSERVER_H

#include "common.h"
#include "network.h"
#include "http.h"

namespace lsxp {

class HttpContext
{
public:
    HttpContext();

    HttpRequest*    request;
    HttpBodyReader* body;
    HttpResponse*   response;
    TcpSocket*      socket;
    std::string     clientIp;
    bool            keepAlive;

    bool ReadBody(std::string& out, int64 maxBytes, std::string& errorText);
};

class IHttpHandler
{
public:
    virtual ~IHttpHandler() {}
    virtual bool Handle(HttpContext& context) = 0;
};

// Small blocking HTTP/1.1 server, one thread per connection: LocalSend
// uploads several files in parallel.
class HttpServer
{
public:
    HttpServer();
    ~HttpServer();

    bool Start(unsigned short port, IHttpHandler* handler, std::string& errorText);
    void Stop();
    bool IsRunning() const;
    unsigned short BoundPort() const { return m_port; }

private:
    struct Connection
    {
        HttpServer* server;
        SOCKET      socket;
        std::string clientIp;
    };

    static DWORD WINAPI ListenEntry(LPVOID parameter);
    static DWORD WINAPI ConnectionEntry(LPVOID parameter);
    void ListenLoop();
    void ServeConnection(SOCKET clientSocket, const std::string& clientIp);
    bool ParseRequestHead(const std::string& head,
                          HttpRequest& request,
                          bool& keepAlive,
                          int64& bodyLength,
                          bool& chunked);
    bool WriteResponse(HttpResponse& response,
                       TcpSocket& socket,
                       bool keepAlive,
                       std::string& errorText);

    TcpListener*  m_listener;
    IHttpHandler* m_handler;
    volatile LONG m_running;
    HANDLE        m_thread;
    unsigned short m_port;
    volatile LONG m_activeConnections;
};

}  // namespace lsxp

#endif  // LSXP_HTTPSERVER_H
