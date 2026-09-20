#ifndef LSXP_HTTP_H
#define LSXP_HTTP_H

#include "common.h"
#include "network.h"

namespace lsxp {

struct HttpHeader
{
    std::string name;
    std::string value;
};

class HttpRequest
{
public:
    HttpRequest();

    std::string method;   // GET / POST
    std::string path;     // "/api/localsend/v2/register"
    std::string version;  // "HTTP/1.1"
    std::vector<HttpHeader> headers;
    std::string body;

    // Optional file streaming source, used for uploads.
    HANDLE fileHandle;
    int64  fileOffset;
    int64  fileLength;

    void SetHeader(const std::string& name, const std::string& value);
    bool Has(const std::string& name) const;
    bool GetHeader(const std::string& name, std::string& value) const;
    std::string HeaderOr(const std::string& name, const std::string& defaultValue) const;
    int64 BodyLength() const;
    std::string BuildHead() const;

private:
    void RemoveHeader(const std::string& name);
};

class HttpResponse
{
public:
    HttpResponse();

    int status;
    std::string reason;
    std::vector<HttpHeader> headers;
    std::string body;

    // Optional file streaming source, used when serving downloads.
    bool         hasFile;
    HANDLE       fileHandle;
    int64        fileOffset;
    int64        fileLength;
    std::wstring fileName;
    bool         closeFileAfterSend;

    void SetHeader(const std::string& name, const std::string& value);
    bool Has(const std::string& name) const;
    bool GetHeader(const std::string& name, std::string& value) const;
    std::string HeaderOr(const std::string& name, const std::string& defaultValue) const;
    int64 ContentLength() const;
    bool  IsChunked() const;

    void SetJson(const std::string& jsonText);
    void SetText(int code, const std::string& reasonText, const std::string& bodyText);

private:
    void RemoveHeader(const std::string& name);
};

// Reads an HTTP request body: fixed length, chunked, or until close.
class HttpBodyReader
{
public:
    HttpBodyReader(TcpSocket* socket, int64 length, bool chunked, const std::string& pendingData);

    int   Read(void* buffer, int capacity);
    int64 Length() const { return m_length; }
    int64 Position() const { return m_read; }
    bool  IsComplete() const { return m_complete; }

private:
    int  ReadFromChunked(void* buffer, int capacity);
    bool FillPending();

    TcpSocket*  m_socket;
    int64       m_length;      // -1 when unknown
    int64       m_read;
    bool        m_chunked;
    bool        m_complete;
    bool        m_chunkEnded;
    std::string m_pending;
};

class HttpClient
{
public:
    // Return false to abort the transfer (used for cancellation).
    typedef bool (*ProgressCallback)(void* context, int64 sent, int64 total);

    static bool Execute(const std::string& ip,
                        unsigned short port,
                        HttpRequest& request,
                        HttpResponse& response,
                        DWORD timeoutMs,
                        std::string& errorText);

    static bool ExecuteStreaming(const std::string& ip,
                                 unsigned short port,
                                 HttpRequest& request,
                                 HttpResponse& response,
                                 DWORD timeoutMs,
                                 ProgressCallback progress,
                                 void* context,
                                 volatile LONG* cancelFlag,
                                 std::string& errorText);

    // Sends the request and streams the response body straight into a file
    // (used by the reverse transfer / download API).
    static bool DownloadToFile(const std::string& ip,
                               unsigned short port,
                               HttpRequest& request,
                               HANDLE targetFile,
                               int64* receivedOut,
                               ProgressCallback progress,
                               void* context,
                               volatile LONG* cancelFlag,
                               HttpResponse& response,
                               std::string& errorText);
};

}  // namespace lsxp

#endif  // LSXP_HTTP_H
