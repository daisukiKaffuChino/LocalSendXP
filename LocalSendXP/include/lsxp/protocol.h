#ifndef LSXP_PROTOCOL_H
#define LSXP_PROTOCOL_H

#include "common.h"
#include "json.h"
#include "config.h"
#include "device.h"
#include "transfer.h"
#include "http.h"
#include "httpserver.h"

namespace lsxp {

struct IncomingFile
{
    IncomingFile();

    std::string id;        // file id used by the sender
    std::string name;      // UTF-8 file name (sanitized)
    uint64      size;
    std::string mimeType;
    std::string sha256;
    std::string token;     // token we handed out
    bool        accepted;
    bool        finished;
};

// Created by an HTTP worker thread, shown by the UI thread.
struct IncomingPrompt
{
    IncomingPrompt();
    ~IncomingPrompt();

    std::string peerAlias;
    std::string peerIp;
    std::string peerVersion;
    std::string sessionId;
    std::vector<IncomingFile> files;
    uint64      totalSize;
    bool        accepted;        // filled in by the UI
    bool        pinMissing;      // filled in by the server
    std::string tokenForPeer;    // v2 token the peer must sign requests with
    HANDLE      doneEvent;
};

struct ShareEntry
{
    std::string  id;
    std::wstring path;
    std::string  name;      // UTF-8
    uint64       size;
    std::string  mimeType;
};

struct ShareSession
{
    std::string sessionId;
    std::vector<ShareEntry> entries;
    uint64      totalSize;
    DWORD       createdTick;
};

// One file offered by a peer through the reverse transfer (download) API.
struct SharedFileInfo
{
    SharedFileInfo();

    std::string id;
    std::string name;      // UTF-8
    uint64      size;
    std::string mimeType;
};

namespace proto {

// ---- shared -------------------------------------------------------------
JsonValue   BuildDeviceInfo(const Config& config, bool includePort, bool includeFingerprint);
Device      DeviceFromInfo(const JsonValue& info, const std::string& ip, unsigned short defaultPort);
std::string NewRandomId();
std::string RequestPath(const std::string& target);
std::string QueryParam(const std::string& target, const std::string& key);

// ---- outgoing (we are the sender) ---------------------------------------
bool SendRegister(const Device& device,
                  const Config& config,
                  Device& updatedDevice,
                  int& httpStatus,
                  std::string& errorText,
                  DWORD timeoutMs = 3000);

bool PrepareUpload(const Device& device,
                   const Config& config,
                   std::vector<TransferFile>& files,
                   const std::string& pin,
                   std::string& sessionId,
                   int& httpStatus,
                   std::string& errorText);

bool UploadFile(const Device& device,
                const Config& config,
                TransferFile& file,
                const std::string& sessionId,
                ITransferProgress* progress,
                int& httpStatus,
                std::string& errorText);

bool CancelUpload(const Device& device,
                  const Config& config,
                  const std::string& sessionId,
                  std::string& errorText);

// ---- legacy v1 protocol -------------------------------------------------
bool SendRegisterV1(const Device& device, const Config& config, Device& updatedDevice, std::string& errorText);
bool PrepareUploadV1(const Device& device, const Config& config, std::vector<TransferFile>& files, int& httpStatus, std::string& errorText);
bool UploadFileV1(const Device& device, const Config& config, TransferFile& file, ITransferProgress* progress, int& httpStatus, std::string& errorText);
bool CancelUploadV1(const Device& device, const Config& config, std::string& errorText);

// ---- incoming, client side of the download API -------------------------
bool ParseShareUrl(const std::string& url,
                   std::string& ip,
                   unsigned short& port,
                   std::string& sessionId,
                   std::string& pin,
                   std::string& errorText);

bool FetchShareList(const std::string& ip,
                    unsigned short port,
                    std::string& sessionId,
                    const std::string& pin,
                    std::string& peerAlias,
                    std::vector<SharedFileInfo>& files,
                    int& httpStatus,
                    std::string& errorText);

bool DownloadSharedFile(const std::string& ip,
                        unsigned short port,
                        const std::string& sessionId,
                        const std::string& fileId,
                        const std::wstring& targetPath,
                        uint64 expectedSize,
                        ITransferProgress* progress,
                        volatile LONG* cancelFlag,
                        int& httpStatus,
                        std::string& errorText);

// ---- incoming (we are the receiver): HTTP API ---------------------------
class ServerHandler : public IHttpHandler
{
public:
    ServerHandler();
    virtual ~ServerHandler();

    void SetSessionToken(const std::string& token) { m_sessionToken = token; }
    virtual bool Handle(HttpContext& context);

private:
    bool HandleRegister(HttpContext& context);
    bool HandlePrepareUpload(HttpContext& context);
    bool HandleUpload(HttpContext& context);
    bool HandleCancel(HttpContext& context);
    bool HandleInfo(HttpContext& context);
    bool HandlePrepareDownload(HttpContext& context);
    bool HandleDownload(HttpContext& context);
    bool HandleBrowserIndex(HttpContext& context);
    bool CheckPin(const std::string& query) const;

    std::string m_sessionToken;
    CRITICAL_SECTION m_cs;
    std::string m_sessionId;
    std::string m_clientIp;
    long        m_transferId;
    bool        m_sessionActive;
    DWORD       m_sessionTick;
    std::map<std::string, std::string>  m_tokens;
    std::map<std::string, std::wstring> m_targets;
    std::map<std::string, uint64>       m_received;
    std::map<std::string, uint64>       m_sizes;
    std::map<std::string, std::string>  m_hashes;
    std::map<std::string, int>          m_fileIndex;
};

// ---- browser sharing (reverse transfer) ---------------------------------
class ShareManager
{
public:
    static ShareManager& Instance();

    ShareManager();
    ~ShareManager();

    bool Create(const std::vector<std::wstring>& paths, std::string& sessionId, std::string& errorText);
    bool Get(const std::string& sessionId, ShareSession& out) const;
    bool GetAny(ShareSession& out) const;
    void Close(const std::string& sessionId);
    void Clear();
    std::wstring BuildUrl(const std::string& sessionId) const;

private:
    mutable CRITICAL_SECTION m_cs;
    std::vector<ShareSession> m_sessions;
};

}  // namespace proto
}  // namespace lsxp

#endif  // LSXP_PROTOCOL_H
