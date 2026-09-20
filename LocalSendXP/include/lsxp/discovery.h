#ifndef LSXP_DISCOVERY_H
#define LSXP_DISCOVERY_H

#include "common.h"
#include "network.h"
#include "device.h"
#include "config.h"

namespace lsxp {

class App;

// UDP multicast announcement + broadcast fallback + legacy HTTP scan.
class DiscoveryService
{
public:
    DiscoveryService();
    ~DiscoveryService();

    bool Start(App* app, Config* config, DeviceManager* devices);
    void Stop();

    void AnnounceNow();
    void RequestScan();
    void SetFingerprint(const std::string& fingerprint) { m_fingerprint = fingerprint; }
    DWORD LastAnnounceTick() const { return m_lastAnnounce; }

private:
    static DWORD WINAPI ThreadEntry(LPVOID parameter);
    static DWORD WINAPI ScanEntry(LPVOID parameter);
    static DWORD WINAPI ScanWorker(LPVOID parameter);

    void Run();
    void HandleDatagram(const std::string& text, const std::string& fromIp, unsigned short fromPort);
    void SendAnnounce();
    void SendAnnounceTo(const std::string& ip, bool announce);
    void RespondToAnnounce(const Device& device, bool v1);
    void RunScan();

    App*           m_app;
    Config*        m_config;
    DeviceManager* m_devices;
    UdpSocket      m_socket;
    HANDLE         m_thread;
    HANDLE         m_scanThread;
    volatile LONG  m_running;
    volatile LONG  m_scanRequest;
    DWORD          m_lastAnnounce;
    std::string    m_fingerprint;
};

}  // namespace lsxp

#endif  // LSXP_DISCOVERY_H
