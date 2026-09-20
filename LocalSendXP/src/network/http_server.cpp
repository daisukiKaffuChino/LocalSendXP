#include "lsxp/httpserver.h"

#include <stdlib.h>

namespace lsxp {

namespace {

const int kMaxConnections = 24;

}  // namespace

HttpContext::HttpContext()
    : request(NULL), body(NULL), response(NULL), socket(NULL), keepAlive(true)
{
}

bool HttpContext::ReadBody(std::string& out, int64 maxBytes, std::string& errorText)
{
    out.clear();
    if (body == NULL)
    {
        return true;
    }

    char temp[8192];
    for (;;)
    {
        int received = body->Read(temp, sizeof(temp));
        if (received < 0)
        {
            errorText = "failed to read the request body";
            return false;
        }
        if (received == 0)
        {
            return true;
        }
        if (maxBytes >= 0 && (int64)out.size() + (int64)received > maxBytes)
        {
            errorText = "request body too large";
            return false;
        }
        out.append(temp, (size_t)received);
    }
}

// ------------------------------------------------------------ body reader
HttpBodyReader::HttpBodyReader(TcpSocket* socket, int64 length, bool chunked,
                               const std::string& pendingData)
    : m_socket(socket),
      m_length(chunked ? -1 : length),
      m_read(0),
      m_chunked(chunked),
      m_complete(false),
      m_chunkEnded(false),
      m_pending(pendingData)
{
}

bool HttpBodyReader::FillPending()
{
    if (!m_pending.empty())
    {
        return true;
    }
    if (m_socket == NULL)
    {
        return false;
    }
    char temp[8192];
    int received = m_socket->Recv(temp, sizeof(temp), 30000);
    if (received <= 0)
    {
        return false;
    }
    m_pending.append(temp, (size_t)received);
    return true;
}

int HttpBodyReader::ReadFromChunked(void* buffer, int capacity)
{
    for (;;)
    {
        if (m_chunkEnded)
        {
            m_complete = true;
            return 0;
        }

        if (m_length >= 0)
        {
            // Inside a chunk: m_length holds the remaining bytes.
            if (m_length == 0)
            {
                while (m_pending.size() < 2)
                {
                    if (!FillPending())
                    {
                        m_complete = true;
                        return -1;
                    }
                }
                m_pending.erase(0, 2);  // trailing CRLF
                m_length = -2;          // read the next chunk size
                continue;
            }

            if (!FillPending())
            {
                m_complete = true;
                return -1;
            }
            size_t available = m_pending.size();
            size_t wanted = (size_t)capacity;
            if ((int64)wanted > m_length)
            {
                wanted = (size_t)m_length;
            }
            if (wanted > available)
            {
                wanted = available;
            }
            memcpy(buffer, m_pending.data(), wanted);
            m_pending.erase(0, wanted);
            m_length -= (int64)wanted;
            m_read += (int64)wanted;
            return (int)wanted;
        }

        // Read the chunk size line.
        size_t endOfLine;
        while ((endOfLine = m_pending.find("\r\n")) == std::string::npos)
        {
            if (m_pending.size() > 4096)
            {
                m_complete = true;
                return -1;
            }
            if (!FillPending())
            {
                m_complete = true;
                return -1;
            }
        }

        std::string line = m_pending.substr(0, endOfLine);
        m_pending.erase(0, endOfLine + 2);
        size_t semicolon = line.find(';');
        if (semicolon != std::string::npos)
        {
            line = line.substr(0, semicolon);
        }
        long chunkSize = strtol(Trim(line).c_str(), NULL, 16);
        if (chunkSize <= 0)
        {
            m_chunkEnded = true;
            m_complete = true;
            return 0;
        }
        m_length = chunkSize;
    }
}

int HttpBodyReader::Read(void* buffer, int capacity)
{
    if (capacity <= 0 || buffer == NULL)
    {
        return 0;
    }
    if (m_complete)
    {
        return 0;
    }
    if (m_chunked)
    {
        return ReadFromChunked(buffer, capacity);
    }

    if (m_length >= 0)
    {
        if (m_read >= m_length)
        {
            m_complete = true;
            return 0;
        }
        int wanted = capacity;
        if ((int64)wanted > m_length - m_read)
        {
            wanted = (int)(m_length - m_read);
        }

        if (!m_pending.empty())
        {
            size_t take = (size_t)wanted;
            if (take > m_pending.size())
            {
                take = m_pending.size();
            }
            memcpy(buffer, m_pending.data(), take);
            m_pending.erase(0, take);
            m_read += (int64)take;
            if (m_read >= m_length)
            {
                m_complete = true;
            }
            return (int)take;
        }

        if (m_socket == NULL)
        {
            m_complete = true;
            return -1;
        }
        int received = m_socket->Recv(buffer, wanted, 30000);
        if (received <= 0)
        {
            m_complete = true;
            return -1;
        }
        m_read += received;
        if (m_read >= m_length)
        {
            m_complete = true;
        }
        return received;
    }

    // Unknown length: read until the peer closes the connection.
    if (!m_pending.empty())
    {
        size_t take = m_pending.size();
        if (take > (size_t)capacity)
        {
            take = (size_t)capacity;
        }
        memcpy(buffer, m_pending.data(), take);
        m_pending.erase(0, take);
        m_read += (int64)take;
        return (int)take;
    }
    if (m_socket == NULL)
    {
        m_complete = true;
        return 0;
    }
    int received = m_socket->Recv(buffer, capacity, 30000);
    if (received <= 0)
    {
        m_complete = true;
        return received;
    }
    m_read += received;
    return received;
}

// ------------------------------------------------------------ http server
HttpServer::HttpServer()
    : m_listener(NULL),
      m_handler(NULL),
      m_running(0),
      m_thread(NULL),
      m_port(0),
      m_activeConnections(0)
{
}

HttpServer::~HttpServer()
{
    Stop();
}

bool HttpServer::Start(unsigned short port, IHttpHandler* handler, std::string& errorText)
{
    if (m_running != 0)
    {
        return true;
    }
    if (handler == NULL)
    {
        errorText = "no request handler";
        return false;
    }

    m_listener = new TcpListener();
    if (!m_listener->Listen(port, "", errorText))
    {
        delete m_listener;
        m_listener = NULL;
        return false;
    }

    m_handler = handler;
    m_port = m_listener->BoundPort();
    InterlockedExchange(&m_running, 1);

    m_thread = CreateThread(NULL, 0, &HttpServer::ListenEntry, this, 0, NULL);
    if (m_thread == NULL)
    {
        InterlockedExchange(&m_running, 0);
        m_listener->Close();
        delete m_listener;
        m_listener = NULL;
        m_handler = NULL;
        errorText = "cannot create the HTTP server thread";
        return false;
    }

    LogLine("HTTP server listening on port %u", (unsigned)m_port);
    return true;
}

void HttpServer::Stop()
{
    if (m_running == 0 && m_listener == NULL)
    {
        return;
    }

    InterlockedExchange(&m_running, 0);
    if (m_listener != NULL)
    {
        m_listener->RequestStop();
        m_listener->Close();
    }
    if (m_thread != NULL)
    {
        WaitForSingleObject(m_thread, 3000);
        CloseHandle(m_thread);
        m_thread = NULL;
    }

    for (int i = 0; i < 60 && m_activeConnections > 0; ++i)
    {
        Sleep(50);
    }

    delete m_listener;
    m_listener = NULL;
    m_handler = NULL;
    m_port = 0;
}

bool HttpServer::IsRunning() const
{
    return m_running != 0;
}

DWORD WINAPI HttpServer::ListenEntry(LPVOID parameter)
{
    HttpServer* server = (HttpServer*)parameter;
    server->ListenLoop();
    return 0;
}

void HttpServer::ListenLoop()
{
    while (m_running != 0 && m_listener != NULL)
    {
        std::string peerIp;
        unsigned short peerPort = 0;
        SOCKET client = m_listener->Accept(250, peerIp, peerPort);
        if (client == INVALID_SOCKET)
        {
            continue;
        }

        if (m_activeConnections >= kMaxConnections)
        {
            const char* busy = "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            send(client, busy, (int)strlen(busy), 0);
            closesocket(client);
            continue;
        }

        Connection* connection = new Connection();
        connection->server = this;
        connection->socket = client;
        connection->clientIp = peerIp;
        InterlockedIncrement(&m_activeConnections);

        HANDLE thread = CreateThread(NULL, 0, &HttpServer::ConnectionEntry, connection, 0, NULL);
        if (thread == NULL)
        {
            InterlockedDecrement(&m_activeConnections);
            closesocket(client);
            delete connection;
        }
        else
        {
            CloseHandle(thread);
        }
    }
}

DWORD WINAPI HttpServer::ConnectionEntry(LPVOID parameter)
{
    Connection* connection = (Connection*)parameter;
    HttpServer* server = connection->server;
    SOCKET socket = connection->socket;
    std::string clientIp = connection->clientIp;
    delete connection;

    server->ServeConnection(socket, clientIp);

    closesocket(socket);
    InterlockedDecrement(&server->m_activeConnections);
    return 0;
}

bool HttpServer::ParseRequestHead(const std::string& head,
                                  HttpRequest& request,
                                  bool& keepAlive,
                                  int64& bodyLength,
                                  bool& chunked)
{
    std::vector<std::string> lines = Split(head, '\n');
    if (lines.empty())
    {
        return false;
    }

    std::string requestLine = Trim(lines[0]);
    std::vector<std::string> parts = Split(requestLine, ' ');
    if (parts.size() < 3)
    {
        return false;
    }

    request.method = parts[0];
    request.path = parts[1];
    request.version = Trim(parts[2]);

    for (size_t i = 1; i < lines.size(); ++i)
    {
        std::string line = Trim(lines[i]);
        if (line.empty())
        {
            continue;
        }
        size_t colon = line.find(':');
        if (colon == std::string::npos)
        {
            continue;
        }
        HttpHeader header;
        header.name = Trim(line.substr(0, colon));
        header.value = Trim(line.substr(colon + 1));
        request.headers.push_back(header);
    }

    std::string connection = ToLower(request.HeaderOr("Connection", ""));
    if (request.version == "HTTP/1.0")
    {
        keepAlive = (connection == "keep-alive");
    }
    else
    {
        keepAlive = (connection != "close");
    }

    std::string transferEncoding = ToLower(request.HeaderOr("Transfer-Encoding", ""));
    chunked = transferEncoding.find("chunked") != std::string::npos;

    if (chunked)
    {
        bodyLength = -1;
    }
    else
    {
        std::string lengthText = request.HeaderOr("Content-Length", "");
        bodyLength = lengthText.empty() ? 0 : (int64)_atoi64(Trim(lengthText).c_str());
        if (bodyLength < 0)
        {
            bodyLength = 0;
        }
    }
    return true;
}

bool HttpServer::WriteResponse(HttpResponse& response,
                               TcpSocket& socket,
                               bool keepAlive,
                               std::string& errorText)
{
    if (!response.Has("Content-Length") && !response.IsChunked())
    {
        uint64 length = response.hasFile ? (uint64)response.fileLength : (uint64)response.body.size();
        response.SetHeader("Content-Length", FormatUInt(length));
    }
    if (!response.Has("Connection"))
    {
        response.SetHeader("Connection", keepAlive ? "keep-alive" : "close");
    }
    if (!response.Has("Server"))
    {
        response.SetHeader("Server", "LocalSendXP/1.0.0");
    }

    if (response.reason.empty())
    {
        response.reason = (response.status == 200) ? "OK" : "Response";
    }

    std::string head = Format("HTTP/1.1 %d %s\r\n", response.status, response.reason.c_str());
    for (size_t i = 0; i < response.headers.size(); ++i)
    {
        head += response.headers[i].name;
        head += ": ";
        head += response.headers[i].value;
        head += "\r\n";
    }
    head += "\r\n";

    if (!socket.SendAll(head.data(), (int)head.size(), errorText))
    {
        return false;
    }

    if (response.hasFile && response.fileHandle != NULL && response.fileHandle != INVALID_HANDLE_VALUE)
    {
        std::vector<unsigned char> buffer(64 * 1024);
        LONG highPart = (LONG)(response.fileOffset >> 32);
        DWORD lowPart = (DWORD)(response.fileOffset & 0xFFFFFFFF);
        SetFilePointer(response.fileHandle, (LONG)lowPart, &highPart, FILE_BEGIN);

        int64 remaining = response.fileLength;
        while (remaining > 0)
        {
            DWORD wanted = (DWORD)(remaining > (int64)buffer.size() ? (int64)buffer.size() : remaining);
            DWORD read = 0;
            if (!ReadFile(response.fileHandle, &buffer[0], wanted, &read, NULL))
            {
                errorText = Format("file read failed (%lu)", GetLastError());
                return false;
            }
            if (read == 0)
            {
                break;
            }
            if (!socket.SendAll(&buffer[0], (int)read, errorText))
            {
                return false;
            }
            remaining -= read;
        }
        if (response.closeFileAfterSend)
        {
            CloseHandle(response.fileHandle);
            response.fileHandle = INVALID_HANDLE_VALUE;
            response.closeFileAfterSend = false;
        }
        return true;
    }

    if (!response.body.empty())
    {
        if (!socket.SendAll(response.body.data(), (int)response.body.size(), errorText))
        {
            return false;
        }
    }
    return true;
}

void HttpServer::ServeConnection(SOCKET clientSocket, const std::string& clientIp)
{
    TcpSocket socket;
    if (!socket.Attach(clientSocket))
    {
        return;
    }
    socket.SetNoDelay(true);

    std::string buffer;
    for (;;)
    {
        size_t headerEnd = buffer.find("\r\n\r\n");
        while (headerEnd == std::string::npos)
        {
            if (buffer.size() > 128 * 1024)
            {
                return;
            }
            char temp[8192];
            int received = socket.Recv(temp, sizeof(temp), 30000);
            if (received <= 0)
            {
                return;
            }
            buffer.append(temp, (size_t)received);
            headerEnd = buffer.find("\r\n\r\n");
        }

        std::string head = buffer.substr(0, headerEnd);
        std::string pending = buffer.substr(headerEnd + 4);
        buffer.clear();

        HttpRequest request;
        bool keepAlive = true;
        int64 bodyLength = 0;
        bool chunked = false;
        if (!ParseRequestHead(head, request, keepAlive, bodyLength, chunked))
        {
            const char* bad = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            send(clientSocket, bad, (int)strlen(bad), 0);
            return;
        }

        std::string sendError;
        if (EqualsNoCase(request.HeaderOr("Expect", ""), "100-continue"))
        {
            const char* cont = "HTTP/1.1 100 Continue\r\n\r\n";
            socket.SendAll(cont, (int)strlen(cont), sendError);
        }

        HttpBodyReader reader(&socket, bodyLength, chunked, pending);
        HttpResponse response;

        HttpContext context;
        context.request = &request;
        context.body = &reader;
        context.response = &response;
        context.socket = &socket;
        context.clientIp = clientIp;
        context.keepAlive = keepAlive;

        bool handled = false;
        if (m_handler != NULL)
        {
            handled = m_handler->Handle(context);
        }
        if (!handled && response.status == 0)
        {
            response.SetText(404, "Not Found", "Not Found");
        }
        if (response.status == 0)
        {
            response.status = 200;
            response.reason = "OK";
        }

        if (!WriteResponse(response, socket, keepAlive, sendError))
        {
            LogLine("response to %s failed: %s", clientIp.c_str(), sendError.c_str());
            return;
        }

        if (!keepAlive)
        {
            return;
        }

        // Drain whatever the handler did not read so the connection stays usable.
        if (!reader.IsComplete())
        {
            char temp[4096];
            while (reader.Read(temp, sizeof(temp)) > 0)
            {
                // discard
            }
        }
        if (!reader.IsComplete())
        {
            return;
        }
    }
}

}  // namespace lsxp
