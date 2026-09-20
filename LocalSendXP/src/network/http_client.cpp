#include "lsxp/http.h"

#include <stdlib.h>

namespace lsxp {

namespace {

const DWORD kIoTimeoutMs = 30000;

bool EqualsName(const std::string& a, const std::string& b)
{
    return EqualsNoCase(a, b);
}

void ParseStatusLine(const std::string& line, HttpResponse& response)
{
    response.status = 0;
    response.reason.clear();

    std::vector<std::string> parts = Split(line, ' ');
    if (parts.size() >= 2)
    {
        response.status = atoi(parts[1].c_str());
    }
    if (parts.size() >= 3)
    {
        for (size_t i = 2; i < parts.size(); ++i)
        {
            if (!response.reason.empty())
            {
                response.reason += " ";
            }
            response.reason += parts[i];
        }
    }
}

void ParseHeaderLines(const std::string& head, HttpResponse& response)
{
    std::vector<std::string> lines = Split(head, '\n');
    if (lines.empty())
    {
        return;
    }
    ParseStatusLine(Trim(lines[0]), response);

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
        std::string name = Trim(line.substr(0, colon));
        std::string value = Trim(line.substr(colon + 1));
        HttpHeader header;
        header.name = name;
        header.value = value;
        response.headers.push_back(header);
    }
}

bool ReadMore(IStream& socket, std::string& buffer, DWORD timeoutMs, std::string& errorText)
{
    char temp[8192];
    int received = socket.Recv(temp, sizeof(temp), timeoutMs);
    if (received <= 0)
    {
        errorText = "connection closed while reading response";
        return false;
    }
    buffer.append(temp, (size_t)received);
    return true;
}

bool ReadChunkedBody(IStream& socket, std::string& data, std::string& out,
                     DWORD timeoutMs, std::string& errorText)
{
    size_t position = 0;
    for (;;)
    {
        size_t endOfLine;
        while ((endOfLine = data.find("\r\n", position)) == std::string::npos)
        {
            if (data.size() > position + 8192)
            {
                errorText = "invalid chunked encoding";
                return false;
            }
            if (!ReadMore(socket, data, timeoutMs, errorText))
            {
                return false;
            }
        }

        std::string sizeLine = data.substr(position, endOfLine - position);
        size_t semicolon = sizeLine.find(';');
        if (semicolon != std::string::npos)
        {
            sizeLine = sizeLine.substr(0, semicolon);
        }
        sizeLine = Trim(sizeLine);
        if (sizeLine.empty())
        {
            errorText = "invalid chunked encoding";
            return false;
        }

        long chunkSize = strtol(sizeLine.c_str(), NULL, 16);
        position = endOfLine + 2;
        if (chunkSize < 0)
        {
            errorText = "invalid chunk size";
            return false;
        }
        if (chunkSize == 0)
        {
            return true;
        }

        while (data.size() < position + (size_t)chunkSize + 2)
        {
            if (!ReadMore(socket, data, timeoutMs, errorText))
            {
                return false;
            }
        }
        out.append(data, position, (size_t)chunkSize);
        position += (size_t)chunkSize + 2;
    }
}

bool PrepareRequest(IStream& socket,
                    const std::string& ip,
                    unsigned short port,
                    HttpRequest& request,
                    std::string& errorText)
{
    if (!request.Has("Host"))
    {
        request.SetHeader("Host", Format("%s:%u", ip.c_str(), (unsigned)port));
    }
    if (!request.Has("Connection"))
    {
        request.SetHeader("Connection", "close");
    }
    if (!request.Has("Content-Length") && !request.Has("Transfer-Encoding"))
    {
        request.SetHeader("Content-Length", FormatUInt((uint64)request.BodyLength()));
    }

    std::string head = request.BuildHead();
    if (!socket.SendAll(head.data(), (int)head.size(), errorText))
    {
        return false;
    }

    if (request.fileHandle == NULL || request.fileHandle == INVALID_HANDLE_VALUE)
    {
        if (!request.body.empty())
        {
            if (!socket.SendAll(request.body.data(), (int)request.body.size(), errorText))
            {
                return false;
            }
        }
    }
    return true;
}

bool ReadResponseHead(IStream& socket, HttpResponse& response, std::string& leftover,
                      DWORD timeoutMs, std::string& errorText)
{
    std::string buffer;

    for (;;)
    {
        size_t headerEnd = buffer.find("\r\n\r\n");
        while (headerEnd == std::string::npos)
        {
            if (buffer.size() > 128 * 1024)
            {
                errorText = "response headers too large";
                return false;
            }
            if (!ReadMore(socket, buffer, timeoutMs, errorText))
            {
                return false;
            }
            headerEnd = buffer.find("\r\n\r\n");
        }

        HttpResponse parsed;
        ParseHeaderLines(buffer.substr(0, headerEnd), parsed);
        leftover = buffer.substr(headerEnd + 4);

        if (parsed.status >= 100 && parsed.status < 200)
        {
            // Interim response (for example "100 Continue"): keep reading.
            buffer = leftover;
            leftover.clear();
            continue;
        }

        response = parsed;
        return true;
    }
}

bool ReadResponse(IStream& socket, HttpResponse& response, DWORD timeoutMs, std::string& errorText)
{
    std::string leftover;
    if (!ReadResponseHead(socket, response, leftover, timeoutMs, errorText))
    {
        return false;
    }

    bool bodyExpected = true;
    if (response.status == 204 || response.status == 304 || response.status == 100)
    {
        bodyExpected = false;
    }

    if (!bodyExpected)
    {
        response.body.clear();
        return true;
    }

    if (response.IsChunked())
    {
        std::string body;
        if (!ReadChunkedBody(socket, leftover, body, timeoutMs, errorText))
        {
            return false;
        }
        response.body = body;
        return true;
    }

    int64 contentLength = response.ContentLength();
    if (contentLength >= 0)
    {
        std::string body = leftover;
        while ((int64)body.size() < contentLength)
        {
            if (!ReadMore(socket, body, timeoutMs, errorText))
            {
                return false;
            }
        }
        if ((int64)body.size() > contentLength)
        {
            body.resize((size_t)contentLength);
        }
        response.body = body;
        return true;
    }

    // No length information: read until the peer closes the connection.
    std::string body = leftover;
    char temp[8192];
    for (;;)
    {
        int received = socket.Recv(temp, sizeof(temp), timeoutMs);
        if (received <= 0)
        {
            break;
        }
        body.append(temp, (size_t)received);
    }
    response.body = body;
    return true;
}

bool WriteAllToFile(HANDLE file, const unsigned char* data, int length, std::string& errorText)
{
    DWORD written = 0;
    if (!WriteFile(file, data, (DWORD)length, &written, NULL) || written != (DWORD)length)
    {
        errorText = Format("cannot write the received data (%lu)", GetLastError());
        return false;
    }
    return true;
}

}  // namespace

HttpRequest::HttpRequest()
    : method("GET"),
      path("/"),
      version("HTTP/1.1"),
      fileHandle(INVALID_HANDLE_VALUE),
      fileOffset(0),
      fileLength(-1)
{
}

HttpConnectionOptions::HttpConnectionOptions()
    : secure(false),
      verifyMode(TLS_VERIFY_FINGERPRINT),
      allowLegacyTls(false)
{
}

void HttpRequest::SetHeader(const std::string& name, const std::string& value)
{
    for (size_t i = 0; i < headers.size(); ++i)
    {
        if (EqualsName(headers[i].name, name))
        {
            headers[i].value = value;
            return;
        }
    }
    HttpHeader header;
    header.name = name;
    header.value = value;
    headers.push_back(header);
}

void HttpRequest::RemoveHeader(const std::string& name)
{
    for (std::vector<HttpHeader>::iterator it = headers.begin(); it != headers.end(); ++it)
    {
        if (EqualsName(it->name, name))
        {
            headers.erase(it);
            return;
        }
    }
}

bool HttpRequest::GetHeader(const std::string& name, std::string& value) const
{
    for (size_t i = 0; i < headers.size(); ++i)
    {
        if (EqualsName(headers[i].name, name))
        {
            value = headers[i].value;
            return true;
        }
    }
    return false;
}

bool HttpRequest::Has(const std::string& name) const
{
    std::string value;
    return GetHeader(name, value);
}

std::string HttpRequest::HeaderOr(const std::string& name, const std::string& defaultValue) const
{
    std::string value;
    if (GetHeader(name, value))
    {
        return value;
    }
    return defaultValue;
}

int64 HttpRequest::BodyLength() const
{
    if (fileHandle != NULL && fileHandle != INVALID_HANDLE_VALUE)
    {
        return fileLength >= 0 ? fileLength : 0;
    }
    return (int64)body.size();
}

std::string HttpRequest::BuildHead() const
{
    std::string out;
    out += method;
    out += ' ';
    out += path.empty() ? "/" : path;
    out += ' ';
    out += version.empty() ? "HTTP/1.1" : version;
    out += "\r\n";

    for (size_t i = 0; i < headers.size(); ++i)
    {
        out += headers[i].name;
        out += ": ";
        out += headers[i].value;
        out += "\r\n";
    }
    out += "\r\n";
    return out;
}

HttpResponse::HttpResponse()
    : status(0),
      hasFile(false),
      fileHandle(INVALID_HANDLE_VALUE),
      fileOffset(0),
      fileLength(0),
      closeFileAfterSend(false)
{
}

void HttpResponse::SetHeader(const std::string& name, const std::string& value)
{
    for (size_t i = 0; i < headers.size(); ++i)
    {
        if (EqualsName(headers[i].name, name))
        {
            headers[i].value = value;
            return;
        }
    }
    HttpHeader header;
    header.name = name;
    header.value = value;
    headers.push_back(header);
}

void HttpResponse::RemoveHeader(const std::string& name)
{
    for (std::vector<HttpHeader>::iterator it = headers.begin(); it != headers.end(); ++it)
    {
        if (EqualsName(it->name, name))
        {
            headers.erase(it);
            return;
        }
    }
}

bool HttpResponse::GetHeader(const std::string& name, std::string& value) const
{
    for (size_t i = 0; i < headers.size(); ++i)
    {
        if (EqualsName(headers[i].name, name))
        {
            value = headers[i].value;
            return true;
        }
    }
    return false;
}

bool HttpResponse::Has(const std::string& name) const
{
    std::string value;
    return GetHeader(name, value);
}

std::string HttpResponse::HeaderOr(const std::string& name, const std::string& defaultValue) const
{
    std::string value;
    if (GetHeader(name, value))
    {
        return value;
    }
    return defaultValue;
}

int64 HttpResponse::ContentLength() const
{
    std::string value;
    if (!GetHeader("Content-Length", value))
    {
        return -1;
    }
    return (int64)_atoi64(Trim(value).c_str());
}

bool HttpResponse::IsChunked() const
{
    std::string value;
    if (!GetHeader("Transfer-Encoding", value))
    {
        return false;
    }
    return ToLower(value).find("chunked") != std::string::npos;
}

void HttpResponse::SetJson(const std::string& jsonText)
{
    SetHeader("Content-Type", "application/json");
    body = jsonText;
}

void HttpResponse::SetText(int code, const std::string& reasonText, const std::string& bodyText)
{
    status = code;
    reason = reasonText;
    SetHeader("Content-Type", "text/plain; charset=utf-8");
    body = bodyText;
}

bool HttpClient::Execute(const std::string& ip,
                         unsigned short port,
                         HttpRequest& request,
                         HttpResponse& response,
                         DWORD timeoutMs,
                         std::string& errorText)
{
    return ExecuteStreaming(ip, port, request, response, timeoutMs, NULL, NULL, NULL, errorText);
}

bool HttpClient::ExecuteStreaming(const std::string& ip,
                                  unsigned short port,
                                  HttpRequest& request,
                                  HttpResponse& response,
                                  DWORD timeoutMs,
                                  ProgressCallback progress,
                                  void* context,
                                  volatile LONG* cancelFlag,
                                  std::string& errorText)
{
    if (!WinsockStartup(errorText))
    {
        return false;
    }

    TcpSocket socket;
    if (!socket.Connect(ip, port, timeoutMs, errorText))
    {
        return false;
    }

    TlsStream tls;
    IStream* stream = &socket;
    if (request.connection.secure)
    {
        const std::string hostName = request.connection.hostName.empty()
                                     ? ip : request.connection.hostName;
        if (!tls.Connect(&socket, hostName, (TlsVerifyMode)request.connection.verifyMode,
                         request.connection.expectedFingerprint,
                         request.connection.allowLegacyTls, errorText))
        {
            return false;
        }
        stream = &tls;
    }

    if (!PrepareRequest(*stream, ip, port, request, errorText))
    {
        return false;
    }

    int64 total = request.BodyLength();
    int64 sent = 0;

    if (request.fileHandle != NULL && request.fileHandle != INVALID_HANDLE_VALUE)
    {
        std::vector<unsigned char> buffer(64 * 1024);
        LONG highPart = (LONG)(request.fileOffset >> 32);
        DWORD lowPart = (DWORD)(request.fileOffset & 0xFFFFFFFF);
        SetFilePointer(request.fileHandle, (LONG)lowPart, &highPart, FILE_BEGIN);

        int64 remaining = total;
        while (remaining > 0)
        {
            if (cancelFlag != NULL && InterlockedCompareExchange((LONG*)cancelFlag, 1, 1) == 1)
            {
                errorText = "canceled";
                return false;
            }

            DWORD wanted = (DWORD)(remaining > (int64)buffer.size() ? (int64)buffer.size() : remaining);
            DWORD read = 0;
            if (!ReadFile(request.fileHandle, &buffer[0], wanted, &read, NULL))
            {
                errorText = Format("read failed (%lu)", GetLastError());
                return false;
            }
            if (read == 0)
            {
                errorText = "file shrank while sending";
                return false;
            }
            if (!stream->SendAll(&buffer[0], (int)read, errorText))
            {
                return false;
            }
            remaining -= read;
            sent += read;
            if (progress != NULL)
            {
                if (!progress(context, sent, total))
                {
                    errorText = "canceled";
                    return false;
                }
            }
        }
    }
    else
    {
        sent = (int64)request.body.size();
        if (progress != NULL)
        {
            if (!progress(context, sent, total))
            {
                errorText = "canceled";
                return false;
            }
        }
    }

    if (!ReadResponse(*stream, response, kIoTimeoutMs, errorText))
    {
        return false;
    }
    return true;
}

bool HttpClient::DownloadToFile(const std::string& ip,
                                unsigned short port,
                                HttpRequest& request,
                                HANDLE targetFile,
                                int64* receivedOut,
                                ProgressCallback progress,
                                void* context,
                                volatile LONG* cancelFlag,
                                HttpResponse& response,
                                std::string& errorText)
{
    if (receivedOut != NULL)
    {
        *receivedOut = 0;
    }
    if (targetFile == NULL || targetFile == INVALID_HANDLE_VALUE)
    {
        errorText = "no target file";
        return false;
    }
    if (!WinsockStartup(errorText))
    {
        return false;
    }

    TcpSocket socket;
    if (!socket.Connect(ip, port, 8000, errorText))
    {
        return false;
    }

    TlsStream tls;
    IStream* stream = &socket;
    if (request.connection.secure)
    {
        const std::string hostName = request.connection.hostName.empty()
                                     ? ip : request.connection.hostName;
        if (!tls.Connect(&socket, hostName, (TlsVerifyMode)request.connection.verifyMode,
                         request.connection.expectedFingerprint,
                         request.connection.allowLegacyTls, errorText))
        {
            return false;
        }
        stream = &tls;
    }

    if (!PrepareRequest(*stream, ip, port, request, errorText))
    {
        return false;
    }

    std::string leftover;
    if (!ReadResponseHead(*stream, response, leftover, kIoTimeoutMs, errorText))
    {
        return false;
    }

    if (response.status != 200)
    {
        // Keep the diagnostic body (it is small) and let the caller report it.
        std::string body = leftover;
        char temp[4096];
        for (;;)
        {
            int received = stream->Recv(temp, sizeof(temp), kIoTimeoutMs);
            if (received <= 0)
            {
                break;
            }
            body.append(temp, (size_t)received);
            if (body.size() > 8192)
            {
                break;
            }
        }
        response.body = body;
        return true;
    }

    int64 length = response.ContentLength();
    int64 total = (response.IsChunked() || length < 0) ? -1 : length;

    HttpBodyReader reader(stream, length, response.IsChunked(), leftover);
    std::vector<unsigned char> buffer(64 * 1024);
    int64 received = 0;

    for (;;)
    {
        if (cancelFlag != NULL && InterlockedCompareExchange((LONG*)cancelFlag, 1, 1) == 1)
        {
            errorText = "canceled";
            return false;
        }

        int count = reader.Read(&buffer[0], (int)buffer.size());
        if (count < 0)
        {
            errorText = "failed while reading the response body";
            return false;
        }
        if (count == 0)
        {
            break;
        }
        if (!WriteAllToFile(targetFile, &buffer[0], count, errorText))
        {
            return false;
        }
        received += (int64)count;
        if (progress != NULL && !progress(context, received, total))
        {
            errorText = "canceled";
            return false;
        }
    }

    if (receivedOut != NULL)
    {
        *receivedOut = received;
    }
    return true;
}

}  // namespace lsxp
