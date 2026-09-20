#include "lsxp/protocol.h"
#include "lsxp/app.h"
#include "lsxp/history.h"
#include "lsxp/sha256.h"
#include "resource.h"

namespace lsxp {
namespace proto {

namespace {

const DWORD kSessionIdleTimeoutMs = 5 * 60 * 1000;

std::string FileModifiedIso(const std::wstring& path)
{
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info))
    {
        return std::string();
    }
    FILETIME utc = info.ftLastWriteTime;
    SYSTEMTIME systemTime;
    if (!FileTimeToSystemTime(&utc, &systemTime))
    {
        return std::string();
    }
    return Iso8601UtcFromSystemTime(systemTime);
}

void BuildFilesJson(const std::vector<TransferFile>& files, JsonValue& out, bool v1)
{
    out = JsonValue::MakeObject();
    for (size_t i = 0; i < files.size(); ++i)
    {
        const TransferFile& file = files[i];

        JsonValue entry = JsonValue::MakeObject();
        entry.Set("id", file.id);
        entry.Set("fileName", file.fileName);
        entry.Set("size", (int64)file.size);
        entry.Set("fileType", v1 ? std::string("other") : file.mimeType);
        entry.Set("preview", JsonValue());
        if (!file.sha256.empty())
        {
            entry.Set("sha256", file.sha256);
        }
        if (!v1)
        {
            std::string modified = FileModifiedIso(file.path);
            if (!modified.empty())
            {
                JsonValue metadata = JsonValue::MakeObject();
                metadata.Set("modified", modified);
                entry.Set("metadata", metadata);
            }
        }
        out.Set(file.id, entry);
    }
}

struct UploadBridge
{
    ITransferProgress* progress;
};

bool UploadBridgeCallback(void* context, int64 sent, int64 total)
{
    UploadBridge* bridge = (UploadBridge*)context;
    if (bridge == NULL || bridge->progress == NULL)
    {
        return true;
    }
    if (bridge->progress->IsCancelRequested())
    {
        return false;
    }
    bridge->progress->OnProgress((uint64)sent, (uint64)total);
    return true;
}

std::string DescribeStatus(int status)
{
    switch (status)
    {
    case 400: return "bad request";
    case 401: return "PIN required";
    case 403: return "rejected by the receiver";
    case 409: return "the receiver is busy with another session";
    case 422: return "checksum mismatch";
    case 429: return "too many requests";
    case 500: return "receiver reported an unknown error";
    default:  return Format("HTTP %d", status);
    }
}

}  // namespace

// ------------------------------------------------------------- outgoing
bool PrepareUpload(const Device& device,
                   const Config& config,
                   std::vector<TransferFile>& files,
                   const std::string& pin,
                   std::string& sessionId,
                   int& httpStatus,
                   std::string& errorText)
{
    if (device.IsProtocolV1())
    {
        return PrepareUploadV1(device, config, files, httpStatus, errorText);
    }

    JsonValue root = JsonValue::MakeObject();
    root.Set("info", BuildDeviceInfo(config, true, true));
    JsonValue filesJson;
    BuildFilesJson(files, filesJson, false);
    root.Set("files", filesJson);

    HttpRequest request;
    request.method = "POST";
    request.path = "/api/localsend/v2/prepare-upload";
    if (!pin.empty())
    {
        request.path += "?pin=" + UrlEncode(pin);
    }
    request.body = root.Serialize();
    request.SetHeader("Content-Type", "application/json");
    request.SetHeader("Content-Length", FormatUInt((uint64)request.body.size()));

    ApplyDeviceSecurity(device, config, request);

    HttpResponse response;
    if (!HttpClient::Execute(device.ip, device.port, request, response, 5000, errorText))
    {
        return false;
    }

    httpStatus = response.status;
    if (response.status == 204)
    {
        sessionId.clear();
        return true;
    }
    if (response.status != 200)
    {
        errorText = DescribeStatus(response.status);
        return false;
    }

    JsonValue result;
    std::string parseError;
    if (!JsonValue::Parse(response.body, result, parseError))
    {
        errorText = "invalid prepare-upload response: " + parseError;
        return false;
    }

    sessionId = result.Get("sessionId").AsString();
    const JsonValue& tokens = result.Get("files");
    if (sessionId.empty() || !tokens.IsObject())
    {
        errorText = "prepare-upload response is incomplete";
        return false;
    }

    for (size_t i = 0; i < files.size(); ++i)
    {
        std::string token = tokens.Get(files[i].id).AsString();
        files[i].token = token;
        files[i].accepted = !token.empty();
    }
    return true;
}

bool UploadFile(const Device& device,
                const Config& config,
                TransferFile& file,
                const std::string& sessionId,
                ITransferProgress* progress,
                int& httpStatus,
                std::string& errorText)
{
    if (device.IsProtocolV1())
    {
        return UploadFileV1(device, config, file, progress, httpStatus, errorText);
    }

    HANDLE handle = CreateFileW(file.path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                                OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (handle == INVALID_HANDLE_VALUE)
    {
        errorText = Format("cannot open %s (%lu)", WideToUtf8(file.path).c_str(), GetLastError());
        return false;
    }

    int64 size = (int64)FileSizeW(file.path);

    HttpRequest request;
    request.method = "POST";
    request.path = Format("/api/localsend/v2/upload?sessionId=%s&fileId=%s&token=%s",
                          UrlEncode(sessionId).c_str(),
                          UrlEncode(file.id).c_str(),
                          UrlEncode(file.token).c_str());
    request.fileHandle = handle;
    request.fileOffset = 0;
    request.fileLength = size;
    request.SetHeader("Content-Type", "application/octet-stream");

    UploadBridge bridge;
    bridge.progress = progress;

    ApplyDeviceSecurity(device, config, request);

    HttpResponse response;
    bool ok = HttpClient::ExecuteStreaming(device.ip, device.port, request, response,
                                           8000, &UploadBridgeCallback, &bridge, NULL, errorText);
    CloseHandle(handle);

    httpStatus = response.status;
    if (!ok)
    {
        return false;
    }
    if (response.status != 200 && response.status != 204)
    {
        errorText = DescribeStatus(response.status);
        return false;
    }
    return true;
}

bool CancelUpload(const Device& device,
                  const Config& config,
                  const std::string& sessionId,
                  std::string& errorText)
{
    (void)config;
    if (device.IsProtocolV1())
    {
        return CancelUploadV1(device, config, errorText);
    }
    if (sessionId.empty())
    {
        return true;
    }

    HttpRequest request;
    request.method = "POST";
    request.path = "/api/localsend/v2/cancel?sessionId=" + UrlEncode(sessionId);
    request.SetHeader("Content-Length", "0");

    ApplyDeviceSecurity(device, config, request);

    HttpResponse response;
    if (!HttpClient::Execute(device.ip, device.port, request, response, 3000, errorText))
    {
        return false;
    }
    return response.status == 200;
}

// --------------------------------------------------------- legacy v1 API
bool PrepareUploadV1(const Device& device,
                     const Config& config,
                     std::vector<TransferFile>& files,
                     int& httpStatus,
                     std::string& errorText)
{
    JsonValue root = JsonValue::MakeObject();

    JsonValue info = JsonValue::MakeObject();
    info.Set("alias", WideToUtf8(config.alias));
    info.Set("deviceModel", WideToUtf8(config.deviceModel));
    info.Set("deviceType", config.deviceType == "mobile" || config.deviceType == "web"
                          ? config.deviceType : std::string("desktop"));
    root.Set("info", info);

    JsonValue filesJson;
    BuildFilesJson(files, filesJson, true);
    root.Set("files", filesJson);

    HttpRequest request;
    request.method = "POST";
    request.path = "/api/localsend/v1/send-request";
    request.body = root.Serialize();
    request.SetHeader("Content-Type", "application/json");
    request.SetHeader("Content-Length", FormatUInt((uint64)request.body.size()));

    ApplyDeviceSecurity(device, config, request);

    HttpResponse response;
    if (!HttpClient::Execute(device.ip, device.port, request, response, 5000, errorText))
    {
        return false;
    }

    httpStatus = response.status;
    if (response.status != 200)
    {
        errorText = DescribeStatus(response.status);
        return false;
    }

    JsonValue tokens;
    std::string parseError;
    if (!JsonValue::Parse(response.body, tokens, parseError))
    {
        errorText = "invalid send-request response: " + parseError;
        return false;
    }

    for (size_t i = 0; i < files.size(); ++i)
    {
        std::string token = tokens.Get(files[i].id).AsString();
        files[i].token = token;
        files[i].accepted = !token.empty();
    }
    return true;
}

bool UploadFileV1(const Device& device,
                  const Config& config,
                  TransferFile& file,
                  ITransferProgress* progress,
                  int& httpStatus,
                  std::string& errorText)
{
    (void)config;

    HANDLE handle = CreateFileW(file.path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                                OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (handle == INVALID_HANDLE_VALUE)
    {
        errorText = Format("cannot open %s (%lu)", WideToUtf8(file.path).c_str(), GetLastError());
        return false;
    }

    HttpRequest request;
    request.method = "POST";
    request.path = Format("/api/localsend/v1/send?fileId=%s&token=%s",
                          UrlEncode(file.id).c_str(), UrlEncode(file.token).c_str());
    request.fileHandle = handle;
    request.fileOffset = 0;
    request.fileLength = (int64)FileSizeW(file.path);
    request.SetHeader("Content-Type", "application/octet-stream");

    UploadBridge bridge;
    bridge.progress = progress;

    ApplyDeviceSecurity(device, config, request);

    HttpResponse response;
    bool ok = HttpClient::ExecuteStreaming(device.ip, device.port, request, response,
                                           8000, &UploadBridgeCallback, &bridge, NULL, errorText);
    CloseHandle(handle);

    httpStatus = response.status;
    if (!ok)
    {
        return false;
    }
    if (response.status != 200)
    {
        errorText = DescribeStatus(response.status);
        return false;
    }
    return true;
}

bool CancelUploadV1(const Device& device, const Config& config, std::string& errorText)
{
    (void)config;
    HttpRequest request;
    request.method = "POST";
    request.path = "/api/localsend/v1/cancel";
    request.SetHeader("Content-Length", "0");

    ApplyDeviceSecurity(device, config, request);

    HttpResponse response;
    if (!HttpClient::Execute(device.ip, device.port, request, response, 3000, errorText))
    {
        return false;
    }
    return response.status == 200;
}

// ------------------------------------------------------------- incoming
bool ServerHandler::CheckPin(const std::string& target) const
{
    const Config& config = App::Instance().GetConfig();
    if (!config.PinRequired())
    {
        return true;
    }
    return QueryParam(target, "pin") == config.pin;
}

bool ServerHandler::HandlePrepareUpload(HttpContext& context)
{
    App& app = App::Instance();
    const Config& config = app.GetConfig();
    std::string target = context.request->path;
    bool v1 = RequestPath(target).find("/v1/") != std::string::npos;

    if (!CheckPin(target))
    {
        context.response->SetText(401, "Unauthorized", "PIN required or invalid");
        return true;
    }

    std::string body;
    std::string errorText;
    if (!context.ReadBody(body, 16 * 1024 * 1024, errorText))
    {
        context.response->SetText(400, "Bad Request", errorText);
        return true;
    }

    JsonValue root;
    std::string parseError;
    if (!JsonValue::Parse(body, root, parseError))
    {
        context.response->SetText(400, "Bad Request", "invalid JSON body");
        return true;
    }

    const JsonValue& filesJson = root.Get("files");
    if (!filesJson.IsObject() || filesJson.Size() == 0)
    {
        context.response->SetText(400, "Bad Request", "no files in request");
        return true;
    }

    EnterCriticalSection(&m_cs);
    bool staleSession = m_sessionActive &&
                        (TickCount() - m_sessionTick) > kSessionIdleTimeoutMs;
    bool busy = m_sessionActive && !staleSession;
    LeaveCriticalSection(&m_cs);

    if (busy)
    {
        context.response->SetText(409, "Conflict", "another session is active");
        return true;
    }
    if (staleSession)
    {
        LogLine("dropping stale incoming session %s", m_sessionId.c_str());
        EnterCriticalSection(&m_cs);
        m_sessionActive = false;
        LeaveCriticalSection(&m_cs);
    }

    Device peer = DeviceFromInfo(root.Get("info"), context.clientIp, (unsigned short)LSXP_DEFAULT_PORT);
    if (v1)
    {
        peer.version = "1.0";
    }

    if (!context.clientFingerprint.empty() && !peer.fingerprint.empty() &&
        !EqualsNoCase(peer.fingerprint, context.clientFingerprint))
    {
        LogLine("upload request from %s rejected: certificate fingerprint mismatch",
                context.clientIp.c_str());
        context.response->SetText(403, "Forbidden", "certificate fingerprint mismatch");
        return true;
    }

    app.Devices().AddOrUpdate(peer, NULL);
    app.NotifyDevicesChanged();

    IncomingPrompt* prompt = new IncomingPrompt();
    prompt->peerAlias = peer.alias;
    prompt->peerIp = context.clientIp;
    prompt->peerVersion = peer.version;
    prompt->sessionId = NewRandomId();
    prompt->totalSize = 0;

    const std::map<std::string, JsonValue>& members = filesJson.Members();
    for (std::map<std::string, JsonValue>::const_iterator it = members.begin();
         it != members.end(); ++it)
    {
        IncomingFile file;
        file.id = it->second.Get("id").AsStringOr(it->first);
        file.name = it->second.Get("fileName").AsStringOr("unnamed");
        file.size = (uint64)it->second.Get("size").AsInt64(0);
        file.mimeType = it->second.Get("fileType").AsStringOr("application/octet-stream");
        file.sha256 = it->second.Get("sha256").AsStringOr("");
        file.accepted = true;
        file.finished = false;
        prompt->files.push_back(file);
        prompt->totalSize += file.size;
    }

    bool accepted = true;
    if (config.askBeforeReceive)
    {
        accepted = app.PromptIncomingTransfer(prompt);
    }
    if (!accepted)
    {
        delete prompt;
        context.response->SetText(403, "Forbidden", "the user rejected the transfer");
        return true;
    }

    int acceptedCount = 0;
    for (size_t i = 0; i < prompt->files.size(); ++i)
    {
        if (prompt->files[i].accepted)
        {
            ++acceptedCount;
        }
    }
    if (acceptedCount == 0)
    {
        delete prompt;
        context.response->status = 204;
        context.response->reason = "No Content";
        return true;
    }

    std::wstring saveDirectory = config.ResolvedDownloadDirectory();
    if (saveDirectory.empty())
    {
        saveDirectory = GetModuleDirectoryW();
    }

    Transfer* transfer = new Transfer();
    transfer->id = app.Transfers().NextId();
    transfer->direction = 1;
    transfer->peerAlias = peer.alias;
    transfer->peerIp = context.clientIp;
    transfer->peerPort = peer.port;
    transfer->sessionId = prompt->sessionId;
    transfer->saveDirectory = WideToUtf8(saveDirectory);
    transfer->state = TS_TRANSFERRING;

    EnterCriticalSection(&m_cs);
    m_sessionId = prompt->sessionId;
    m_clientIp = context.clientIp;
    m_sessionActive = true;
    m_sessionTick = TickCount();
    m_transferId = transfer->id;
    m_tokens.clear();
    m_targets.clear();
    m_received.clear();
    m_sizes.clear();
    m_hashes.clear();
    m_fileIndex.clear();
    LeaveCriticalSection(&m_cs);

    JsonValue tokenMap = JsonValue::MakeObject();
    int fileIndex = 0;
    for (size_t i = 0; i < prompt->files.size(); ++i)
    {
        const IncomingFile& source = prompt->files[i];
        if (!source.accepted)
        {
            continue;
        }

        std::string token = RandomHex(8);

        TransferFile file;
        file.id = source.id;
        file.token = token;
        file.fileName = source.name;
        file.size = source.size;
        file.mimeType = source.mimeType;
        file.sha256 = source.sha256;
        file.accepted = true;

        std::wstring fileName = SanitizeFileNameW(Utf8ToWide(source.name));
        std::wstring targetPath = MakeUniquePathW(JoinPathW(saveDirectory, fileName));
        file.path = targetPath;

        transfer->files.push_back(file);

        EnterCriticalSection(&m_cs);
        m_tokens[source.id] = token;
        m_targets[source.id] = targetPath;
        m_received[source.id] = 0;
        m_sizes[source.id] = source.size;
        m_hashes[source.id] = source.sha256;
        m_fileIndex[source.id] = fileIndex;
        LeaveCriticalSection(&m_cs);

        tokenMap.Set(source.id, token);
        ++fileIndex;
    }

    app.Transfers().Add(transfer);
    app.NotifyTransfersChanged();

    if (v1)
    {
        context.response->status = 200;
        context.response->reason = "OK";
        context.response->SetJson(tokenMap.Serialize());
    }
    else
    {
        JsonValue result = JsonValue::MakeObject();
        result.Set("sessionId", prompt->sessionId);
        result.Set("files", tokenMap);
        context.response->status = 200;
        context.response->reason = "OK";
        context.response->SetJson(result.Serialize());
    }

    LogLine("incoming session %s from %s: %d file(s) accepted",
            prompt->sessionId.c_str(), context.clientIp.c_str(), acceptedCount);

    delete prompt;
    return true;
}

bool ServerHandler::HandleUpload(HttpContext& context)
{
    App& app = App::Instance();
    std::string target = context.request->path;
    bool v1 = RequestPath(target).find("/v1/") != std::string::npos;

    std::string fileId = QueryParam(target, "fileId");
    std::string token = QueryParam(target, "token");
    std::string sessionId = QueryParam(target, "sessionId");

    std::wstring targetPath;
    uint64 expectedSize = 0;
    std::string expectedHash;
    long transferId = 0;
    int fileIndex = -1;
    bool valid = false;

    EnterCriticalSection(&m_cs);
    if (m_sessionActive && !fileId.empty() && m_tokens.find(fileId) != m_tokens.end())
    {
        bool sessionOk = v1 || (sessionId == m_sessionId);
        bool tokenOk = (m_tokens[fileId] == token);
        bool ipOk = (m_clientIp == context.clientIp);
        if (sessionOk && tokenOk && ipOk)
        {
            valid = true;
            targetPath = m_targets[fileId];
            expectedSize = m_sizes[fileId];
            expectedHash = m_hashes[fileId];
            transferId = m_transferId;
            fileIndex = m_fileIndex[fileId];
            m_sessionTick = TickCount();
        }
    }
    LeaveCriticalSection(&m_cs);

    if (!valid)
    {
        EnterCriticalSection(&m_cs);
        bool sessionActive = m_sessionActive;
        LeaveCriticalSection(&m_cs);

        if (!sessionActive)
        {
            context.response->SetText(409, "Conflict", "no active session");
        }
        else
        {
            context.response->SetText(403, "Forbidden", "invalid token, session or source address");
        }
        return true;
    }

    HANDLE file = CreateFileW(targetPath.c_str(), GENERIC_WRITE, 0, NULL,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        context.response->SetText(500, "Internal Server Error",
                                  "cannot create the target file");
        return true;
    }

    Sha256 hash;
    std::vector<unsigned char> buffer(64 * 1024);
    uint64 received = 0;
    DWORD lastNotify = 0;
    bool writeOk = true;
    bool canceled = false;

    for (;;)
    {
        if (app.m_quitting != 0)
        {
            canceled = true;
            break;
        }
        Transfer* transfer = app.Transfers().Find(transferId);
        if (transfer == NULL || transfer->cancelRequested != 0)
        {
            canceled = true;
            break;
        }

        int count = context.body->Read(&buffer[0], (int)buffer.size());
        if (count < 0)
        {
            writeOk = false;
            break;
        }
        if (count == 0)
        {
            break;
        }

        DWORD written = 0;
        if (!WriteFile(file, &buffer[0], (DWORD)count, &written, NULL) || written != (DWORD)count)
        {
            writeOk = false;
            break;
        }

        hash.Update(&buffer[0], (size_t)count);
        received += (uint64)count;

        if ((TickCount() - lastNotify) >= 150 || (expectedSize != 0 && received >= expectedSize))
        {
            lastNotify = TickCount();
            app.Transfers().SetFileProgress(transferId, fileIndex, received, false);
            app.Transfers().RecomputeDone(transferId);
            app.NotifyProgress();
        }
    }

    CloseHandle(file);

    if (canceled)
    {
        DeleteFileW(targetPath.c_str());
        app.Transfers().SetState(transferId, TS_CANCELED, "canceled");
        app.NotifyTransfersChanged();
        context.response->SetText(403, "Forbidden", "transfer canceled");
        return true;
    }

    if (!writeOk)
    {
        DeleteFileW(targetPath.c_str());
        app.Transfers().SetFileError(transferId, fileIndex, "write failed");
        app.Transfers().SetState(transferId, TS_FAILED, "write failed");
        app.NotifyTransfersChanged();
        context.response->SetText(500, "Internal Server Error", "cannot write the file");
        return true;
    }

    if (!expectedHash.empty())
    {
        unsigned char digest[32];
        hash.Final(digest);
        std::string actual = Sha256::Hex(digest);
        if (!EqualsNoCase(actual, expectedHash))
        {
            DeleteFileW(targetPath.c_str());
            app.Transfers().SetFileError(transferId, fileIndex, "checksum mismatch");
            app.Transfers().SetState(transferId, TS_FAILED, "checksum mismatch");
            app.NotifyTransfersChanged();
            LogLine("checksum mismatch for %s", fileId.c_str());
            context.response->SetText(422, "Unprocessable Entity", "sha256 mismatch");
            return true;
        }
    }

    if (expectedSize != 0 && expectedSize != received)
    {
        LogLine("received %I64u bytes, expected %I64u for %s", received, expectedSize, fileId.c_str());
    }

    // Remember the saved file so that it can be found again from the history
    // window ("open containing folder").
    {
        std::string alias;
        std::string peerIp = m_clientIp;
        Transfer* record = app.Transfers().Find(transferId);
        if (record != NULL)
        {
            alias = record->peerAlias;
            if (!record->peerIp.empty())
            {
                peerIp = record->peerIp;
            }
        }
        HistoryStore::Instance().AddReceived(alias, peerIp, targetPath, received);
    }

    app.Transfers().SetFileProgress(transferId, fileIndex, received, true);
    app.Transfers().RecomputeDone(transferId);
    app.NotifyProgress();

    bool allDone = true;
    EnterCriticalSection(&m_cs);
    m_received[fileId] = received;

    for (std::map<std::string, uint64>::const_iterator it = m_sizes.begin(); it != m_sizes.end(); ++it)
    {
        std::map<std::string, uint64>::const_iterator got = m_received.find(it->first);
        if (got == m_received.end())
        {
            allDone = false;
            break;
        }
        if (it->second != 0 && got->second != it->second)
        {
            allDone = false;
            break;
        }
    }
    if (allDone)
    {
        m_sessionActive = false;
    }
    LeaveCriticalSection(&m_cs);

    if (allDone)
    {
        app.Transfers().SetState(transferId, TS_DONE, "");

        Transfer* finished = app.Transfers().Find(transferId);
        std::wstring directory;
        int fileCount = 0;
        if (finished != NULL)
        {
            directory = Utf8ToWide(finished->saveDirectory);
            fileCount = finished->FileCount();
        }
        app.Balloon(LoadStr(IDS_MSG_NOTIFY_TITLE),
                    FormatStr(IDS_MSG_RECEIVED, fileCount, directory.c_str()));
        if (!directory.empty() && app.GetConfig().openFolderAfterReceive)
        {
            OpenPathWithShell(directory);
        }
    }
    app.NotifyTransfersChanged();

    context.response->status = 200;
    context.response->reason = "OK";
    return true;
}

bool ServerHandler::HandleCancel(HttpContext& context)
{
    App& app = App::Instance();
    std::string sessionId = QueryParam(context.request->path, "sessionId");

    EnterCriticalSection(&m_cs);
    bool matches = m_sessionActive && (sessionId.empty() || sessionId == m_sessionId);
    long transferId = m_transferId;
    if (matches)
    {
        m_sessionActive = false;
    }
    LeaveCriticalSection(&m_cs);

    if (matches && transferId != 0)
    {
        Transfer* transfer = app.Transfers().Find(transferId);
        if (transfer != NULL)
        {
            InterlockedExchange(&transfer->cancelRequested, 1);
            app.Transfers().SetState(transferId, TS_CANCELED, "canceled by the sender");
            app.NotifyTransfersChanged();
        }
    }

    context.response->status = 200;
    context.response->reason = "OK";
    return true;
}

}  // namespace proto
}  // namespace lsxp
