#include "lsxp/protocol.h"
#include "lsxp/app.h"
#include "lsxp/sha256.h"

namespace lsxp {

IncomingFile::IncomingFile()
    : size(0),
      accepted(true),
      finished(false)
{
}

IncomingPrompt::IncomingPrompt()
    : totalSize(0),
      accepted(false),
      pinMissing(false),
      doneEvent(NULL)
{
}

IncomingPrompt::~IncomingPrompt()
{
    if (doneEvent != NULL)
    {
        CloseHandle(doneEvent);
        doneEvent = NULL;
    }
}

namespace proto {

// --------------------------------------------------------------- helpers
std::string RequestPath(const std::string& target)
{
    size_t question = target.find('?');
    if (question == std::string::npos)
    {
        return target;
    }
    return target.substr(0, question);
}

std::string QueryParam(const std::string& target, const std::string& key)
{
    size_t question = target.find('?');
    if (question == std::string::npos)
    {
        return std::string();
    }
    std::string query = target.substr(question + 1);
    std::vector<std::string> pairs = Split(query, '&');
    for (size_t i = 0; i < pairs.size(); ++i)
    {
        if (pairs[i].empty())
        {
            continue;
        }
        size_t equals = pairs[i].find('=');
        std::string name = (equals == std::string::npos) ? pairs[i] : pairs[i].substr(0, equals);
        if (!EqualsNoCase(UrlDecode(name), key))
        {
            continue;
        }
        if (equals == std::string::npos)
        {
            return std::string();
        }
        return UrlDecode(pairs[i].substr(equals + 1));
    }
    return std::string();
}

std::string NewRandomId()
{
    return RandomHex(8);
}

JsonValue BuildDeviceInfo(const Config& config, bool includePort, bool includeFingerprint)
{
    JsonValue info = JsonValue::MakeObject();
    info.Set("alias", WideToUtf8(config.alias));
    info.Set("version", std::string(LSXP_PROTOCOL_VERSION));
    info.Set("deviceModel", WideToUtf8(config.deviceModel));
    info.Set("deviceType", config.deviceType);
    if (includeFingerprint)
    {
        info.Set("fingerprint", config.fingerprint);
    }
    if (includePort)
    {
        info.Set("port", config.port);
        info.Set("protocol", config.ProtocolName());
        info.Set("download", true);
    }
    return info;
}

namespace {

// Protocol v1 only knows mobile / desktop / web.
std::string DeviceTypeForV1(const std::string& deviceType)
{
    if (deviceType == "mobile" || deviceType == "web")
    {
        return deviceType;
    }
    return "desktop";
}

JsonValue BuildDeviceInfoV1(const Config& config, bool includeFingerprint)
{
    JsonValue info = JsonValue::MakeObject();
    info.Set("alias", WideToUtf8(config.alias));
    info.Set("deviceModel", WideToUtf8(config.deviceModel));
    info.Set("deviceType", DeviceTypeForV1(config.deviceType));
    if (includeFingerprint)
    {
        info.Set("fingerprint", config.fingerprint);
    }
    return info;
}

JsonValue BuildResponseInfo(const Config& config, bool v1)
{
    if (v1)
    {
        JsonValue info = BuildDeviceInfoV1(config, true);
        return info;
    }
    JsonValue info = BuildDeviceInfo(config, false, true);
    info.Set("download", true);
    return info;
}

}  // namespace

Device DeviceFromInfo(const JsonValue& info, const std::string& ip, unsigned short defaultPort)
{
    Device device;
    device.ip = ip;
    device.alias = info.Get("alias").AsStringOr("");
    device.version = info.Get("version").AsStringOr("");
    device.deviceModel = info.Get("deviceModel").AsStringOr("");
    device.deviceType = info.Get("deviceType").AsStringOr("desktop");
    device.fingerprint = info.Get("fingerprint").AsStringOr("");
    device.download = info.Get("download").AsBool(false);

    int port = (int)info.Get("port").AsInt64((int64)defaultPort);
    device.port = (unsigned short)((port > 0 && port <= 65535) ? port : (int)defaultPort);

    device.protocol = info.Get("protocol").AsStringOr("http");
    if (device.protocol != "https")
    {
        device.protocol = "http";
    }

    if (device.version.empty())
    {
        device.version = "1.0";
    }
    if (device.deviceType != "desktop" && device.deviceType != "mobile" &&
        device.deviceType != "web" && device.deviceType != "headless" &&
        device.deviceType != "server")
    {
        device.deviceType = "desktop";
    }
    if (device.alias.empty())
    {
        device.alias = ip;
    }
    return device;
}

// ------------------------------------------------------- outgoing register
bool SendRegister(const Device& device,
                  const Config& config,
                  Device& updatedDevice,
                  int& httpStatus,
                  std::string& errorText,
                  DWORD timeoutMs)
{
    bool v1 = device.IsProtocolV1();

    HttpRequest request;
    request.method = "POST";
    request.path = v1 ? "/api/localsend/v1/register" : "/api/localsend/v2/register";
    request.body = (v1 ? BuildDeviceInfoV1(config, true) : BuildDeviceInfo(config, true, true)).Serialize();
    request.SetHeader("Content-Type", "application/json");
    request.SetHeader("Content-Length", FormatUInt((uint64)request.body.size()));

    HttpResponse response;
    if (!HttpClient::Execute(device.ip, device.port, request, response, timeoutMs, errorText))
    {
        return false;
    }

    httpStatus = response.status;
    if (response.status != 200)
    {
        errorText = Format("register returned HTTP %d", response.status);
        return false;
    }

    JsonValue info;
    std::string parseError;
    if (!JsonValue::Parse(response.body, info, parseError))
    {
        errorText = "invalid register response: " + parseError;
        return false;
    }

    updatedDevice = DeviceFromInfo(info, device.ip, device.port);
    updatedDevice.version = device.version;
    updatedDevice.protocol = device.protocol;
    updatedDevice.port = device.port;
    if (updatedDevice.fingerprint.empty())
    {
        updatedDevice.fingerprint = device.fingerprint;
    }
    return true;
}

bool SendRegisterV1(const Device& device, const Config& config, Device& updatedDevice, std::string& errorText)
{
    HttpRequest request;
    request.method = "POST";
    request.path = "/api/localsend/v1/register";
    request.body = BuildDeviceInfoV1(config, true).Serialize();
    request.SetHeader("Content-Type", "application/json");
    request.SetHeader("Content-Length", FormatUInt((uint64)request.body.size()));

    HttpResponse response;
    if (!HttpClient::Execute(device.ip, device.port, request, response, 3000, errorText))
    {
        return false;
    }
    if (response.status != 200)
    {
        errorText = Format("register returned HTTP %d", response.status);
        return false;
    }

    JsonValue info;
    std::string parseError;
    if (!JsonValue::Parse(response.body, info, parseError))
    {
        errorText = "invalid register response: " + parseError;
        return false;
    }

    updatedDevice = DeviceFromInfo(info, device.ip, device.port);
    updatedDevice.version = "1.0";
    updatedDevice.port = device.port;
    return true;
}

// -------------------------------------------------------- server handler
ServerHandler::ServerHandler()
    : m_transferId(0),
      m_sessionActive(false),
      m_sessionTick(0)
{
    InitializeCriticalSection(&m_cs);
}

ServerHandler::~ServerHandler()
{
    DeleteCriticalSection(&m_cs);
}

bool ServerHandler::Handle(HttpContext& context)
{
    std::string path = RequestPath(context.request->path);

    if (path == "/api/localsend/v2/register" || path == "/api/localsend/v1/register")
    {
        return HandleRegister(context);
    }
    if (path == "/api/localsend/v2/info" || path == "/api/localsend/v1/info")
    {
        return HandleInfo(context);
    }
    if (path == "/api/localsend/v2/prepare-upload" || path == "/api/localsend/v1/send-request")
    {
        return HandlePrepareUpload(context);
    }
    if (path == "/api/localsend/v2/upload" || path == "/api/localsend/v1/send")
    {
        return HandleUpload(context);
    }
    if (path == "/api/localsend/v2/cancel" || path == "/api/localsend/v1/cancel")
    {
        return HandleCancel(context);
    }
    if (path == "/api/localsend/v2/prepare-download")
    {
        return HandlePrepareDownload(context);
    }
    if (path == "/api/localsend/v2/download")
    {
        return HandleDownload(context);
    }
    if (path == "/" || path == "/index.html")
    {
        return HandleBrowserIndex(context);
    }

    context.response->SetText(404, "Not Found", "Not Found");
    return true;
}

bool ServerHandler::HandleRegister(HttpContext& context)
{
    App& app = App::Instance();
    const Config& config = app.GetConfig();
    bool v1 = RequestPath(context.request->path).find("/v1/") != std::string::npos;

    std::string body;
    std::string errorText;
    if (!context.ReadBody(body, 256 * 1024, errorText))
    {
        context.response->SetText(400, "Bad Request", errorText);
        return true;
    }

    JsonValue info;
    std::string parseError;
    if (!JsonValue::Parse(body, info, parseError))
    {
        context.response->SetText(400, "Bad Request", "invalid JSON body");
        return true;
    }

    Device device = DeviceFromInfo(info, context.clientIp, (unsigned short)LSXP_DEFAULT_PORT);
    if (v1)
    {
        device.version = "1.0";
    }

    if (!device.fingerprint.empty() && device.fingerprint == config.fingerprint)
    {
        // A stale announcement from ourselves: answer, but do not list it.
        context.response->SetJson(BuildResponseInfo(config, v1).Serialize());
        context.response->status = 200;
        context.response->reason = "OK";
        return true;
    }

    bool isNew = false;
    app.Devices().AddOrUpdate(device, &isNew);
    LogLine("register from %s (%s) port %u", context.clientIp.c_str(),
            device.alias.c_str(), (unsigned)device.port);
    app.NotifyDevicesChanged();

    context.response->status = 200;
    context.response->reason = "OK";
    context.response->SetJson(BuildResponseInfo(config, v1).Serialize());
    return true;
}

bool ServerHandler::HandleInfo(HttpContext& context)
{
    App& app = App::Instance();
    const Config& config = app.GetConfig();
    bool v1 = RequestPath(context.request->path).find("/v1/") != std::string::npos;

    context.response->status = 200;
    context.response->reason = "OK";
    context.response->SetJson(BuildResponseInfo(config, v1).Serialize());
    return true;
}

}  // namespace proto
}  // namespace lsxp
