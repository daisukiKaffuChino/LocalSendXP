#ifndef LSXP_DEVICE_H
#define LSXP_DEVICE_H

#include "common.h"

namespace lsxp {

struct Device
{
    Device();

    std::string    ip;
    unsigned short port;
    std::string    protocol;      // "http" (we only speak http)
    std::string    alias;         // UTF-8
    std::string    version;       // protocol version announced by the peer
    std::string    deviceModel;   // UTF-8
    std::string    deviceType;    // desktop | mobile | web | headless | server
    std::string    fingerprint;
    bool           download;
    DWORD          lastSeen;

    bool IsProtocolV1() const;
    bool IsValid() const { return !ip.empty() && port != 0; }
};

class DeviceManager
{
public:
    DeviceManager();
    ~DeviceManager();

    void AddOrUpdate(const Device& device, bool* isNew);
    bool Find(const std::string& ip, unsigned short port, Device& out) const;
    bool Remove(const std::string& ip, unsigned short port);
    int  RemoveExpired(DWORD maxAgeMs);
    bool Touch(const std::string& ip, unsigned short port);
    void Clear();

    void   Snapshot(std::vector<Device>& out) const;
    size_t Count() const;

private:
    mutable CRITICAL_SECTION m_cs;
    std::vector<Device>      m_devices;
};

}  // namespace lsxp

#endif  // LSXP_DEVICE_H
