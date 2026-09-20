#include "lsxp/discovery.h"
#include "lsxp/app.h"
#include "lsxp/protocol.h"
#include "lsxp/json.h"

namespace lsxp {

namespace {

const DWORD kDeviceExpiryMs = 90 * 1000;
const int     kScanThreads   = 8;

struct ScanJob
{
    DiscoveryService* service;
    std::vector<std::string>* targets;
    volatile LONG* index;
    Config* config;
    App* app;
};

std::string SubnetPrefix(const std::string& ip)
{
    size_t lastDot = ip.find_last_of('.');
    if (lastDot == std::string::npos)
    {
        return std::string();
    }
    return ip.substr(0, lastDot);
}

}  // namespace

DiscoveryService::DiscoveryService()
    : m_app(NULL),
      m_config(NULL),
      m_devices(NULL),
      m_thread(NULL),
      m_scanThread(NULL),
      m_running(0),
      m_scanRequest(0),
      m_lastAnnounce(0)
{
}

DiscoveryService::~DiscoveryService()
{
    Stop();
}

bool DiscoveryService::Start(App* app, Config* config, DeviceManager* devices)
{
    m_app = app;
    m_config = config;
    m_devices = devices;
    if (m_fingerprint.empty())
    {
        // HTTPS mode advertises the certificate hash instead; the caller sets
        // that before starting us.
        m_fingerprint = config->fingerprint;
    }

    std::string errorText;
    if (!m_socket.Open((unsigned short)config->port, true, errorText))
    {
        LogLine("discovery: cannot open UDP port %d: %s", config->port, errorText.c_str());
        return false;
    }

    if (!m_socket.JoinMulticast(LSXP_MULTICAST_GROUP, errorText))
    {
        LogLine("discovery: multicast join failed, falling back to broadcast: %s",
                errorText.c_str());
    }

    m_socket.SetBroadcast(true);
    m_socket.SetMulticastLoop(false);  // ignore our own announcements
    m_socket.SetMulticastTtl(1);

    InterlockedExchange(&m_running, 1);
    m_thread = CreateThread(NULL, 0, &DiscoveryService::ThreadEntry, this, 0, NULL);
    if (m_thread == NULL)
    {
        InterlockedExchange(&m_running, 0);
        m_socket.Close();
        errorText = "cannot create the discovery thread";
        return false;
    }

    LogLine("discovery: listening on UDP %u (multicast %s)",
            (unsigned)config->port, LSXP_MULTICAST_GROUP);
    SendAnnounce();
    return true;
}

void DiscoveryService::Stop()
{
    InterlockedExchange(&m_running, 0);
    m_socket.Close();

    if (m_thread != NULL)
    {
        WaitForSingleObject(m_thread, 3000);
        CloseHandle(m_thread);
        m_thread = NULL;
    }
    if (m_scanThread != NULL)
    {
        WaitForSingleObject(m_scanThread, 5000);
        CloseHandle(m_scanThread);
        m_scanThread = NULL;
    }
}

void DiscoveryService::AnnounceNow()
{
    SendAnnounce();
}

void DiscoveryService::RequestScan()
{
    InterlockedExchange(&m_scanRequest, 1);
}

DWORD WINAPI DiscoveryService::ThreadEntry(LPVOID parameter)
{
    DiscoveryService* service = (DiscoveryService*)parameter;
    service->Run();
    return 0;
}

void DiscoveryService::Run()
{
    std::vector<char> buffer(64 * 1024);
    DWORD lastCleanup = TickCount();

    while (m_running != 0)
    {
        std::string fromIp;
        unsigned short fromPort = 0;
        int received = m_socket.RecvFrom(&buffer[0], (int)buffer.size(), fromIp, fromPort, 200);
        if (received > 0)
        {
            std::string text(&buffer[0], (size_t)received);
            HandleDatagram(text, fromIp, fromPort);
        }

        DWORD now = TickCount();

        int interval = (m_config != NULL) ? m_config->announceIntervalSec * 1000 : 30000;
        if ((now - m_lastAnnounce) >= (DWORD)interval)
        {
            SendAnnounce();
        }

        if (InterlockedCompareExchange(&m_scanRequest, 0, 1) == 1)
        {
            if (m_scanThread == NULL)
            {
                m_scanThread = CreateThread(NULL, 0, &DiscoveryService::ScanEntry, this, 0, NULL);
                if (m_scanThread == NULL)
                {
                    LogLine("discovery: cannot start the network scan thread");
                }
            }
        }

        if (m_scanThread != NULL && WaitForSingleObject(m_scanThread, 0) == WAIT_OBJECT_0)
        {
            CloseHandle(m_scanThread);
            m_scanThread = NULL;
        }

        if ((now - lastCleanup) >= 15000)
        {
            lastCleanup = now;
            if (m_devices != NULL && m_devices->RemoveExpired(kDeviceExpiryMs) > 0)
            {
                if (m_app != NULL)
                {
                    m_app->NotifyDevicesChanged();
                }
            }
        }
    }
}

void DiscoveryService::HandleDatagram(const std::string& text,
                                      const std::string& fromIp,
                                      unsigned short fromPort)
{
    JsonValue message;
    std::string parseError;
    if (!JsonValue::Parse(text, message, parseError))
    {
        LogLine("discovery: ignoring malformed announcement from %s", fromIp.c_str());
        return;
    }

    // Protocol v1 uses "announcement", v2 uses "announce".
    bool v1 = !message.Has("version");
    bool announce = v1 ? message.Get("announcement").AsBool(false)
                       : message.Get("announce").AsBool(false);

    std::string fingerprint = message.Get("fingerprint").AsString();
    if (!fingerprint.empty() && fingerprint == m_fingerprint)
    {
        return;
    }

    Device device = proto::DeviceFromInfo(message, fromIp, fromPort);
    if (v1)
    {
        device.version = "1.0";
    }

    bool isNew = false;
    m_devices->AddOrUpdate(device, &isNew);
    if (m_app != NULL)
    {
        m_app->NotifyDevicesChanged();
    }
    if (isNew)
    {
        LogLine("discovery: found %s (%s:%u) protocol %s",
                device.alias.c_str(), device.ip.c_str(),
                (unsigned)device.port, device.version.c_str());
    }

    if (announce)
    {
        RespondToAnnounce(device, v1);
    }
}

void DiscoveryService::RespondToAnnounce(const Device& device, bool v1)
{
    Device updated;
    int httpStatus = 0;
    std::string errorText;

    bool ok = false;
    if (!v1)
    {
        ok = proto::SendRegister(device, *m_config, updated, httpStatus, errorText);
    }
    else
    {
        ok = proto::SendRegisterV1(device, *m_config, updated, errorText);
    }

    if (ok)
    {
        m_devices->AddOrUpdate(updated, NULL);
        if (m_app != NULL)
        {
            m_app->NotifyDevicesChanged();
        }
        LogLine("discovery: registered with %s (%s)", updated.alias.c_str(), updated.ip.c_str());
    }
    else
    {
        // Multicast/UDP fallback response, as described by the protocol.
        LogLine("discovery: register with %s failed (%s), answering over UDP",
                device.ip.c_str(), errorText.c_str());
        SendAnnounceTo(device.ip, false);
    }
}

void DiscoveryService::SendAnnounce()
{
    m_lastAnnounce = TickCount();

    JsonValue message = proto::BuildDeviceInfo(*m_config, true, true);
    message.Set("announce", true);
    std::string text = message.Serialize();

    std::string errorText;
    if (!m_socket.SendTo(std::string(LSXP_MULTICAST_GROUP),
                         (unsigned short)m_config->port,
                         text.data(), (int)text.size(), errorText))
    {
        LogLine("discovery: multicast announce failed: %s", errorText.c_str());
    }

    // Broadcast fallback for networks that drop multicast.
    m_socket.SendTo("255.255.255.255", (unsigned short)m_config->port,
                    text.data(), (int)text.size(), errorText);

    std::vector<std::string> addresses;
    GetLocalIPv4List(addresses);
    for (size_t i = 0; i < addresses.size(); ++i)
    {
        std::string prefix = SubnetPrefix(addresses[i]);
        if (prefix.empty())
        {
            continue;
        }
        std::string broadcast = prefix + ".255";
        if (broadcast == addresses[i])
        {
            continue;
        }
        m_socket.SendTo(broadcast, (unsigned short)m_config->port,
                        text.data(), (int)text.size(), errorText);
    }
}

void DiscoveryService::SendAnnounceTo(const std::string& ip, bool announce)
{
    JsonValue message = proto::BuildDeviceInfo(*m_config, true, true);
    message.Set("announce", announce);

    std::string text = message.Serialize();
    std::string errorText;
    m_socket.SendTo(ip, (unsigned short)m_config->port, text.data(), (int)text.size(), errorText);
}

DWORD WINAPI DiscoveryService::ScanEntry(LPVOID parameter)
{
    DiscoveryService* service = (DiscoveryService*)parameter;
    service->RunScan();
    return 0;
}

void DiscoveryService::RunScan()
{
    std::vector<std::string> locals;
    GetLocalIPv4List(locals);

    std::vector<std::string> targets;
    for (size_t i = 0; i < locals.size(); ++i)
    {
        std::string prefix = SubnetPrefix(locals[i]);
        if (prefix.empty())
        {
            continue;
        }
        for (int host = 1; host <= 254; ++host)
        {
            std::string candidate = Format("%s.%d", prefix.c_str(), host);
            if (candidate == locals[i])
            {
                continue;
            }
            targets.push_back(candidate);
        }
    }

    if (targets.empty())
    {
        return;
    }

    LogLine("discovery: scanning %d addresses on port %d",
            (int)targets.size(), m_config->port);

    volatile LONG index = 0;
    ScanJob job;
    job.service = this;
    job.targets = &targets;
    job.index = &index;
    job.config = m_config;
    job.app = m_app;

    HANDLE threads[kScanThreads];
    int created = 0;
    for (int i = 0; i < kScanThreads; ++i)
    {
        threads[i] = CreateThread(NULL, 0, &DiscoveryService::ScanWorker, &job, 0, NULL);
        if (threads[i] == NULL)
        {
            break;
        }
        ++created;
    }

    if (created == 0)
    {
        ScanWorker(&job);
    }
    else
    {
        WaitForMultipleObjects(created, threads, TRUE, 60000);
        for (int i = 0; i < created; ++i)
        {
            CloseHandle(threads[i]);
        }
    }

    LogLine("discovery: network scan finished");
}

DWORD WINAPI DiscoveryService::ScanWorker(LPVOID parameter)
{
    ScanJob* job = (ScanJob*)parameter;

    for (;;)
    {
        LONG position = InterlockedIncrement(job->index) - 1;
        if (position < 0 || position >= (LONG)job->targets->size())
        {
            break;
        }

        const std::string& ip = (*job->targets)[position];

        Device device;
        device.ip = ip;
        device.port = (unsigned short)job->config->port;
        device.version = LSXP_PROTOCOL_VERSION;

        Device updated;
        int httpStatus = 0;
        std::string errorText;
        if (proto::SendRegister(device, *job->config, updated, httpStatus, errorText, 400))
        {
            bool isNew = false;
            job->service->m_devices->AddOrUpdate(updated, &isNew);
            if (job->app != NULL)
            {
                job->app->NotifyDevicesChanged();
            }
            LogLine("discovery: scan found %s at %s", updated.alias.c_str(), ip.c_str());
        }
    }
    return 0;
}

}  // namespace lsxp
