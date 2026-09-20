#include "lsxp/protocol.h"
#include "lsxp/app.h"
#include "resource.h"

namespace lsxp {

SharedFileInfo::SharedFileInfo()
    : size(0)
{
}

namespace proto {

namespace {

ShareManager g_shareManager;

std::string DescribeStatus(int status)
{
    switch (status)
    {
    case 400: return "bad request";
    case 401: return "PIN required";
    case 403: return "the sender rejected the request";
    case 404: return "nothing to download";
    case 429: return "too many requests";
    case 500: return "the sender reported an error";
    default:  return Format("HTTP %d", status);
    }
}

struct DownloadBridge
{
    ITransferProgress* progress;
};

// HTTPS URLs typed by the user have no LocalSend fingerprint, so the trust
// anchor is the CA bundle (and only the explicit opt-in disables checking).
void ApplyShareSecurity(bool secure, const std::string& host,
                        const Config& config, HttpRequest& request)
{
    request.connection.secure = secure;
    request.connection.allowLegacyTls = config.allowLegacyTls;
    if (!secure)
    {
        return;
    }
    request.connection.hostName = host;
    request.connection.verifyMode = config.allowInsecureHttps
                                    ? TLS_VERIFY_ALLOW_INSECURE
                                    : TLS_VERIFY_CA;
}

bool DownloadBridgeCallback(void* context, int64 received, int64 total)
{
    DownloadBridge* bridge = (DownloadBridge*)context;
    if (bridge == NULL || bridge->progress == NULL)
    {
        return true;
    }
    if (bridge->progress->IsCancelRequested())
    {
        return false;
    }
    bridge->progress->OnProgress((uint64)received, (uint64)(total < 0 ? 0 : total));
    return true;
}

std::string HtmlEscape(const std::string& text)
{
    std::string out;
    for (size_t i = 0; i < text.size(); ++i)
    {
        switch (text[i])
        {
        case '&':  out += "&amp;";  break;
        case '<':  out += "&lt;";   break;
        case '>':  out += "&gt;";   break;
        case '"':  out += "&quot;"; break;
        default:   out += text[i];  break;
        }
    }
    return out;
}

const ShareEntry* FindEntry(const ShareSession& session, const std::string& id)
{
    for (size_t i = 0; i < session.entries.size(); ++i)
    {
        if (session.entries[i].id == id)
        {
            return &session.entries[i];
        }
    }
    return NULL;
}

}  // namespace

// ------------------------------------------------------------ ShareManager
ShareManager::ShareManager()
{
    InitializeCriticalSection(&m_cs);
}

ShareManager::~ShareManager()
{
    DeleteCriticalSection(&m_cs);
}

ShareManager& ShareManager::Instance()
{
    return g_shareManager;
}

bool ShareManager::Create(const std::vector<std::wstring>& paths,
                          std::string& sessionId,
                          std::string& errorText)
{
    ShareSession session;
    session.sessionId = RandomHex(8);
    session.totalSize = 0;
    session.createdTick = TickCount();

    for (size_t i = 0; i < paths.size(); ++i)
    {
        if (!FileExistsW(paths[i]))
        {
            continue;
        }
        ShareEntry entry;
        entry.id = Format("f%u", (unsigned)i);
        entry.path = paths[i];
        entry.name = WideToUtf8(FileNameFromPathW(paths[i]));
        entry.size = FileSizeW(paths[i]);
        entry.mimeType = MimeTypeFromFileName(entry.name);
        session.totalSize += entry.size;
        session.entries.push_back(entry);
    }

    if (session.entries.empty())
    {
        errorText = "no files to share";
        return false;
    }

    EnterCriticalSection(&m_cs);
    m_sessions.clear();
    m_sessions.push_back(session);
    LeaveCriticalSection(&m_cs);

    sessionId = session.sessionId;
    LogLine("browser share session %s with %d file(s)", sessionId.c_str(), (int)session.entries.size());
    return true;
}

bool ShareManager::Get(const std::string& sessionId, ShareSession& out) const
{
    bool found = false;
    EnterCriticalSection(&m_cs);
    for (size_t i = 0; i < m_sessions.size(); ++i)
    {
        if (m_sessions[i].sessionId == sessionId)
        {
            out = m_sessions[i];
            found = true;
            break;
        }
    }
    LeaveCriticalSection(&m_cs);
    return found;
}

bool ShareManager::GetAny(ShareSession& out) const
{
    bool found = false;
    EnterCriticalSection(&m_cs);
    if (!m_sessions.empty())
    {
        out = m_sessions[m_sessions.size() - 1];
        found = true;
    }
    LeaveCriticalSection(&m_cs);
    return found;
}

void ShareManager::Close(const std::string& sessionId)
{
    EnterCriticalSection(&m_cs);
    for (std::vector<ShareSession>::iterator it = m_sessions.begin(); it != m_sessions.end(); ++it)
    {
        if (it->sessionId == sessionId)
        {
            m_sessions.erase(it);
            break;
        }
    }
    LeaveCriticalSection(&m_cs);
}

void ShareManager::Clear()
{
    EnterCriticalSection(&m_cs);
    m_sessions.clear();
    LeaveCriticalSection(&m_cs);
}

std::wstring ShareManager::BuildUrl(const std::string& sessionId) const
{
    const Config& config = App::Instance().GetConfig();
    std::string ip = GetPrimaryLocalIPv4();
    std::string url = Format("http://%s:%d/?sessionId=%s",
                             ip.c_str(), config.port, sessionId.c_str());
    return Utf8ToWide(url);
}

// ------------------------------------------------------ download API (v2)
bool ServerHandler::HandlePrepareDownload(HttpContext& context)
{
    App& app = App::Instance();
    const Config& config = app.GetConfig();
    std::string target = context.request->path;

    if (!CheckPin(target))
    {
        context.response->SetText(401, "Unauthorized", "PIN required or invalid");
        return true;
    }

    std::string requested = QueryParam(target, "sessionId");
    ShareSession session;
    bool found = requested.empty()
                 ? ShareManager::Instance().GetAny(session)
                 : ShareManager::Instance().Get(requested, session);
    if (!found)
    {
        context.response->SetText(404, "Not Found", "no shared files");
        return true;
    }

    JsonValue files = JsonValue::MakeObject();
    for (size_t i = 0; i < session.entries.size(); ++i)
    {
        const ShareEntry& entry = session.entries[i];
        JsonValue item = JsonValue::MakeObject();
        item.Set("id", entry.id);
        item.Set("fileName", entry.name);
        item.Set("size", (int64)entry.size);
        item.Set("fileType", entry.mimeType);
        item.Set("sha256", JsonValue());
        item.Set("preview", JsonValue());
        files.Set(entry.id, item);
    }

    JsonValue result = JsonValue::MakeObject();
    result.Set("info", BuildDeviceInfo(config, true, true));
    result.Set("sessionId", session.sessionId);
    result.Set("files", files);

    context.response->status = 200;
    context.response->reason = "OK";
    context.response->SetJson(result.Serialize());
    return true;
}

bool ServerHandler::HandleDownload(HttpContext& context)
{
    std::string target = context.request->path;
    std::string fileId = QueryParam(target, "fileId");
    std::string sessionId = QueryParam(target, "sessionId");

    ShareSession session;
    bool found = sessionId.empty()
                 ? ShareManager::Instance().GetAny(session)
                 : ShareManager::Instance().Get(sessionId, session);
    if (!found)
    {
        context.response->SetText(404, "Not Found", "session not found");
        return true;
    }

    const ShareEntry* entry = FindEntry(session, fileId);
    if (entry == NULL)
    {
        context.response->SetText(404, "Not Found", "file not found");
        return true;
    }

    HANDLE file = CreateFileW(entry->path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                              OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        context.response->SetText(500, "Internal Server Error", "cannot open the file");
        return true;
    }

    context.response->status = 200;
    context.response->reason = "OK";
    context.response->hasFile = true;
    context.response->fileHandle = file;
    context.response->fileOffset = 0;
    context.response->fileLength = (int64)entry->size;
    context.response->closeFileAfterSend = true;
    context.response->fileName = Utf8ToWide(entry->name);
    context.response->SetHeader("Content-Type", entry->mimeType);
    context.response->SetHeader("Content-Length", FormatUInt(entry->size));
    context.response->SetHeader("Content-Disposition",
                                Format("attachment; filename=\"download\"; filename*=UTF-8''%s",
                                       UrlEncode(entry->name).c_str()));
    return true;
}

bool ServerHandler::HandleBrowserIndex(HttpContext& context)
{
    const Config& config = App::Instance().GetConfig();

    std::string requested = QueryParam(context.request->path, "sessionId");
    ShareSession session;
    bool found = ShareManager::Instance().GetAny(session);

    std::string html;
    html += "<!DOCTYPE html>\r\n<html><head>\r\n";
    html += "<meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\">\r\n";
    html += "<title>LocalSend XP</title>\r\n";
    html += "<style type=\"text/css\">\r\n";
    html += "body { background: #ece9d8; color: #000; font-family: Tahoma, SimSun, sans-serif;"
            " font-size: 12px; margin: 16px; }\r\n";
    html += "h1 { font-size: 16px; margin: 0 0 4px 0; }\r\n";
    html += "table { border-collapse: collapse; background: #fff; border: 1px solid #7f9db9; width: 100%; }\r\n";
    html += "th { text-align: left; background: #d4d0c8; border-bottom: 1px solid #7f9db9;"
            " padding: 3px 6px; font-weight: normal; }\r\n";
    html += "td { border-bottom: 1px solid #e0e0e0; padding: 3px 6px; }\r\n";
    html += "a { color: #00309c; }\r\n";
    html += ".info { margin: 0 0 10px 0; color: #444; }\r\n";
    html += "</style></head><body>\r\n";

    html += "<h1>" + HtmlEscape(WideToUtf8(config.alias)) + "</h1>\r\n";
    html += "<p class=\"info\">LocalSend XP &middot; " +
            HtmlEscape(GetPrimaryLocalIPv4()) + "</p>\r\n";

    if (!found || session.entries.empty())
    {
        html += "<p>" + HtmlEscape(WideToUtf8(LoadStr(IDS_WEB_EMPTY))) + "</p>\r\n";
    }
    else
    {
        if (!requested.empty() && requested != session.sessionId)
        {
            html += "<p>" + HtmlEscape(WideToUtf8(LoadStr(IDS_WEB_STALE))) + "</p>\r\n";
        }

        std::wstring sizeText = FormatBytesW(session.totalSize);
        std::wstring summary = FormatStr(IDS_WEB_TOTAL, (int)session.entries.size(), sizeText.c_str());
        html += "<p class=\"info\">" + HtmlEscape(WideToUtf8(summary)) + "</p>\r\n";

        html += "<table>\r\n<tr><th>" + HtmlEscape(WideToUtf8(LoadStr(IDS_WEB_COL_FILE))) +
                "</th><th style=\"width:90px\">" +
                HtmlEscape(WideToUtf8(LoadStr(IDS_WEB_COL_SIZE))) + "</th></tr>\r\n";

        for (size_t i = 0; i < session.entries.size(); ++i)
        {
            const ShareEntry& entry = session.entries[i];
            std::string link = Format("/api/localsend/v2/download?sessionId=%s&fileId=%s",
                                      UrlEncode(session.sessionId).c_str(),
                                      UrlEncode(entry.id).c_str());
            html += "<tr><td><a href=\"" + link + "\">" + HtmlEscape(entry.name) +
                    "</a></td><td>" + HtmlEscape(WideToUtf8(FormatBytesW(entry.size))) + "</td></tr>\r\n";
        }
        html += "</table>\r\n";
    }

    html += "</body></html>\r\n";

    context.response->status = 200;
    context.response->reason = "OK";
    context.response->SetHeader("Content-Type", "text/html; charset=utf-8");
    context.response->body = html;
    return true;
}

// ---------------------------------------- client side of the download API
bool ParseShareUrl(const std::string& url,
                   std::string& ip,
                   unsigned short& port,
                   std::string& sessionId,
                   std::string& pin,
                   bool& secure,
                   std::string& errorText)
{
    secure = false;
    std::string text = Trim(url);
    if (text.empty())
    {
        errorText = "empty url";
        return false;
    }
    if (text.size() > 2 && text[0] == '"' && text[text.size() - 1] == '"')
    {
        text = text.substr(1, text.size() - 2);
    }

    std::string lower = ToLower(text);
    if (StartsWith(lower, "https://"))
    {
        secure = true;
        text = text.substr(8);
    }
    else if (StartsWith(lower, "http://"))
    {
        text = text.substr(7);
    }

    size_t separator = text.find_first_of("/?");
    std::string authority = (separator == std::string::npos) ? text : text.substr(0, separator);
    std::string query = (separator == std::string::npos) ? std::string() : text.substr(separator);

    if (authority.empty())
    {
        errorText = "no host";
        return false;
    }

    std::string host = authority;
    port = (unsigned short)LSXP_DEFAULT_PORT;

    size_t colon = authority.find(':');
    if (colon != std::string::npos)
    {
        host = authority.substr(0, colon);
        int value = atoi(authority.substr(colon + 1).c_str());
        if (value < 1 || value > 65535)
        {
            errorText = "invalid port";
            return false;
        }
        port = (unsigned short)value;
    }
    if (host.empty())
    {
        errorText = "no host";
        return false;
    }

    if (inet_addr(host.c_str()) == INADDR_NONE)
    {
        struct hostent* entry = gethostbyname(host.c_str());
        if (entry == NULL || entry->h_addr_list == NULL || entry->h_addr_list[0] == NULL)
        {
            errorText = "cannot resolve host";
            return false;
        }
        struct in_addr address;
        memcpy(&address, entry->h_addr_list[0], sizeof(address));
        ip = inet_ntoa(address);
    }
    else
    {
        ip = host;
    }

    sessionId = QueryParam(query, "sessionId");
    pin = QueryParam(query, "pin");
    return true;
}

bool FetchShareList(const std::string& ip,
                    unsigned short port,
                    std::string& sessionId,
                    const std::string& pin,
                    bool secure,
                    const Config& config,
                    std::string& peerAlias,
                    std::vector<SharedFileInfo>& files,
                    int& httpStatus,
                    std::string& errorText)
{
    files.clear();
    peerAlias.clear();

    HttpRequest request;
    request.method = "POST";
    request.path = "/api/localsend/v2/prepare-download";

    std::string query;
    if (!pin.empty())
    {
        query += "pin=" + UrlEncode(pin);
    }
    if (!sessionId.empty())
    {
        if (!query.empty())
        {
            query += "&";
        }
        query += "sessionId=" + UrlEncode(sessionId);
    }
    if (!query.empty())
    {
        request.path += "?" + query;
    }
    request.SetHeader("Content-Type", "application/json");
    request.SetHeader("Content-Length", "0");
    ApplyShareSecurity(secure, ip, config, request);

    HttpResponse response;
    if (!HttpClient::Execute(ip, port, request, response, 5000, errorText))
    {
        return false;
    }

    httpStatus = response.status;
    if (response.status == 401)
    {
        errorText = "PIN required";
        return false;
    }
    if (response.status != 200)
    {
        errorText = DescribeStatus(response.status);
        return false;
    }

    JsonValue root;
    std::string parseError;
    if (!JsonValue::Parse(response.body, root, parseError))
    {
        errorText = "invalid response: " + parseError;
        return false;
    }

    std::string ownSession = root.Get("sessionId").AsString();
    if (!ownSession.empty())
    {
        sessionId = ownSession;
    }
    peerAlias = root.Get("info").Get("alias").AsStringOr(ip);

    const JsonValue& fileMap = root.Get("files");
    if (fileMap.IsObject())
    {
        const std::map<std::string, JsonValue>& members = fileMap.Members();
        for (std::map<std::string, JsonValue>::const_iterator it = members.begin();
             it != members.end(); ++it)
        {
            SharedFileInfo file;
            file.id = it->second.Get("id").AsStringOr(it->first);
            file.name = it->second.Get("fileName").AsStringOr("unnamed");
            file.size = (uint64)it->second.Get("size").AsInt64(0);
            file.mimeType = it->second.Get("fileType").AsStringOr("application/octet-stream");
            files.push_back(file);
        }
    }
    return true;
}

bool DownloadSharedFile(const std::string& ip,
                        unsigned short port,
                        const std::string& sessionId,
                        const std::string& fileId,
                        bool secure,
                        const Config& config,
                        const std::wstring& targetPath,
                        uint64 expectedSize,
                        ITransferProgress* progress,
                        volatile LONG* cancelFlag,
                        int& httpStatus,
                        std::string& errorText)
{
    HANDLE file = CreateFileW(targetPath.c_str(), GENERIC_WRITE, 0, NULL,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        errorText = Format("cannot create %s (%lu)",
                           WideToUtf8(targetPath).c_str(), GetLastError());
        return false;
    }

    HttpRequest request;
    request.method = "GET";
    request.path = Format("/api/localsend/v2/download?sessionId=%s&fileId=%s",
                          UrlEncode(sessionId).c_str(), UrlEncode(fileId).c_str());
    request.SetHeader("Accept", "application/octet-stream");
    ApplyShareSecurity(secure, ip, config, request);

    DownloadBridge bridge;
    bridge.progress = progress;

    HttpResponse response;
    int64 received = 0;
    bool ok = HttpClient::DownloadToFile(ip, port, request, file, &received,
                                         &DownloadBridgeCallback, &bridge,
                                         cancelFlag, response, errorText);
    CloseHandle(file);

    httpStatus = response.status;
    if (!ok)
    {
        DeleteFileW(targetPath.c_str());
        return false;
    }
    if (response.status != 200)
    {
        DeleteFileW(targetPath.c_str());
        errorText = DescribeStatus(response.status);
        return false;
    }
    if (expectedSize != 0 && (uint64)received != expectedSize)
    {
        LogLine("download: %s size mismatch (%I64u received, %I64u expected)",
                fileId.c_str(), received, expectedSize);
    }
    return true;
}

}  // namespace proto
}  // namespace lsxp
