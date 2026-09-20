#include "lsxp/device.h"

namespace lsxp {

Device::Device()
    : port(0),
      protocol("http"),
      version(LSXP_PROTOCOL_VERSION),
      deviceType("desktop"),
      download(false),
      lastSeen(0)
{
}

bool Device::IsProtocolV1() const
{
    if (version.empty())
    {
        return false;
    }
    return version[0] == '1';
}

DeviceManager::DeviceManager()
{
    InitializeCriticalSection(&m_cs);
}

DeviceManager::~DeviceManager()
{
    DeleteCriticalSection(&m_cs);
}

void DeviceManager::AddOrUpdate(const Device& device, bool* isNew)
{
    if (isNew != NULL)
    {
        *isNew = false;
    }
    if (!device.IsValid())
    {
        return;
    }

    EnterCriticalSection(&m_cs);

    bool found = false;
    for (size_t i = 0; i < m_devices.size(); ++i)
    {
        Device& existing = m_devices[i];
        if (existing.ip == device.ip && existing.port == device.port)
        {
            existing = device;
            existing.lastSeen = TickCount();
            found = true;
            break;
        }
    }

    if (!found)
    {
        Device added = device;
        added.lastSeen = TickCount();
        m_devices.push_back(added);
        if (isNew != NULL)
        {
            *isNew = true;
        }
    }

    LeaveCriticalSection(&m_cs);
}

bool DeviceManager::Find(const std::string& ip, unsigned short port, Device& out) const
{
    bool found = false;
    EnterCriticalSection(&m_cs);
    for (size_t i = 0; i < m_devices.size(); ++i)
    {
        if (m_devices[i].ip == ip && m_devices[i].port == port)
        {
            out = m_devices[i];
            found = true;
            break;
        }
    }
    LeaveCriticalSection(&m_cs);
    return found;
}

bool DeviceManager::Touch(const std::string& ip, unsigned short port)
{
    bool found = false;
    EnterCriticalSection(&m_cs);
    for (size_t i = 0; i < m_devices.size(); ++i)
    {
        if (m_devices[i].ip == ip && m_devices[i].port == port)
        {
            m_devices[i].lastSeen = TickCount();
            found = true;
            break;
        }
    }
    LeaveCriticalSection(&m_cs);
    return found;
}

bool DeviceManager::Remove(const std::string& ip, unsigned short port)
{
    bool removed = false;
    EnterCriticalSection(&m_cs);
    for (std::vector<Device>::iterator it = m_devices.begin(); it != m_devices.end(); ++it)
    {
        if (it->ip == ip && it->port == port)
        {
            m_devices.erase(it);
            removed = true;
            break;
        }
    }
    LeaveCriticalSection(&m_cs);
    return removed;
}

int DeviceManager::RemoveExpired(DWORD maxAgeMs)
{
    int removed = 0;
    DWORD now = TickCount();

    EnterCriticalSection(&m_cs);
    for (std::vector<Device>::iterator it = m_devices.begin(); it != m_devices.end(); )
    {
        if (now - it->lastSeen > maxAgeMs)
        {
            it = m_devices.erase(it);
            ++removed;
        }
        else
        {
            ++it;
        }
    }
    LeaveCriticalSection(&m_cs);
    return removed;
}

void DeviceManager::Clear()
{
    EnterCriticalSection(&m_cs);
    m_devices.clear();
    LeaveCriticalSection(&m_cs);
}

void DeviceManager::Snapshot(std::vector<Device>& out) const
{
    EnterCriticalSection(&m_cs);
    out = m_devices;
    LeaveCriticalSection(&m_cs);
}

size_t DeviceManager::Count() const
{
    EnterCriticalSection(&m_cs);
    size_t count = m_devices.size();
    LeaveCriticalSection(&m_cs);
    return count;
}

}  // namespace lsxp
