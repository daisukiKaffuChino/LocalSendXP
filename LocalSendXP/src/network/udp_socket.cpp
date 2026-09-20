#include "lsxp/network.h"

namespace lsxp {

namespace {

bool g_winsockReady = false;

}  // namespace

bool WinsockStartup(std::string& errorText)
{
    if (g_winsockReady)
    {
        return true;
    }

    WSADATA data;
    int result = WSAStartup(MAKEWORD(2, 2), &data);
    if (result != 0)
    {
        errorText = Format("WSAStartup failed (%d)", result);
        return false;
    }
    if (LOBYTE(data.wVersion) != 2 || HIBYTE(data.wVersion) != 2)
    {
        errorText = "Winsock 2.2 is not available";
        WSACleanup();
        return false;
    }
    g_winsockReady = true;
    return true;
}

void WinsockCleanup()
{
    if (g_winsockReady)
    {
        WSACleanup();
        g_winsockReady = false;
    }
}

UdpSocket::UdpSocket()
    : m_socket(INVALID_SOCKET), m_port(0)
{
}

UdpSocket::~UdpSocket()
{
    Close();
}

bool UdpSocket::Open(unsigned short port, bool reuseAddress, std::string& errorText)
{
    Close();

    m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_socket == INVALID_SOCKET)
    {
        errorText = Format("socket() failed (%d)", WSAGetLastError());
        return false;
    }

    if (reuseAddress)
    {
        BOOL yes = TRUE;
        setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
    }

    sockaddr_in address;
    ZeroMemory(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);

    if (bind(m_socket, (sockaddr*)&address, sizeof(address)) == SOCKET_ERROR)
    {
        errorText = Format("bind() on port %u failed (%d)", (unsigned)port, WSAGetLastError());
        Close();
        return false;
    }

    // Resolve the real port (needed when port 0 was requested).
    sockaddr_in bound;
    int boundLength = sizeof(bound);
    ZeroMemory(&bound, sizeof(bound));
    if (getsockname(m_socket, (sockaddr*)&bound, &boundLength) == 0)
    {
        m_port = ntohs(bound.sin_port);
    }
    else
    {
        m_port = port;
    }
    return true;
}

bool UdpSocket::JoinMulticast(const std::string& group, std::string& errorText)
{
    if (m_socket == INVALID_SOCKET)
    {
        errorText = "socket is not open";
        return false;
    }

    ip_mreq request;
    ZeroMemory(&request, sizeof(request));
    request.imr_multiaddr.s_addr = inet_addr(group.c_str());
    request.imr_interface.s_addr = htonl(INADDR_ANY);

    if (setsockopt(m_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                   (const char*)&request, sizeof(request)) == SOCKET_ERROR)
    {
        int error = WSAGetLastError();
        // Already a member is not a real failure for our purposes.
        if (error != WSAEADDRINUSE)
        {
            errorText = Format("IP_ADD_MEMBERSHIP failed (%d)", error);
            return false;
        }
    }
    return true;
}

void UdpSocket::LeaveMulticast(const std::string& group)
{
    if (m_socket == INVALID_SOCKET)
    {
        return;
    }
    ip_mreq request;
    ZeroMemory(&request, sizeof(request));
    request.imr_multiaddr.s_addr = inet_addr(group.c_str());
    request.imr_interface.s_addr = htonl(INADDR_ANY);
    setsockopt(m_socket, IPPROTO_IP, IP_DROP_MEMBERSHIP, (const char*)&request, sizeof(request));
}

void UdpSocket::SetBroadcast(bool enable)
{
    if (m_socket == INVALID_SOCKET)
    {
        return;
    }
    BOOL value = enable ? TRUE : FALSE;
    setsockopt(m_socket, SOL_SOCKET, SO_BROADCAST, (const char*)&value, sizeof(value));
}

void UdpSocket::SetMulticastLoop(bool enable)
{
    if (m_socket == INVALID_SOCKET)
    {
        return;
    }
    BOOL value = enable ? TRUE : FALSE;
    setsockopt(m_socket, IPPROTO_IP, IP_MULTICAST_LOOP, (const char*)&value, sizeof(value));
}

void UdpSocket::SetMulticastTtl(int ttl)
{
    if (m_socket == INVALID_SOCKET)
    {
        return;
    }
    int value = ttl;
    setsockopt(m_socket, IPPROTO_IP, IP_MULTICAST_TTL, (const char*)&value, sizeof(value));
}

bool UdpSocket::SendTo(const std::string& ip, unsigned short port,
                       const void* data, int length, std::string& errorText)
{
    if (m_socket == INVALID_SOCKET)
    {
        errorText = "socket is not open";
        return false;
    }

    sockaddr_in target;
    ZeroMemory(&target, sizeof(target));
    target.sin_family = AF_INET;
    target.sin_port = htons(port);
    target.sin_addr.s_addr = inet_addr(ip.c_str());
    if (target.sin_addr.s_addr == INADDR_NONE)
    {
        errorText = "invalid address " + ip;
        return false;
    }

    int sent = sendto(m_socket, (const char*)data, length, 0,
                      (sockaddr*)&target, sizeof(target));
    if (sent == SOCKET_ERROR)
    {
        errorText = Format("sendto(%s:%u) failed (%d)", ip.c_str(), (unsigned)port, WSAGetLastError());
        return false;
    }
    return true;
}

int UdpSocket::RecvFrom(void* buffer, int capacity, std::string& fromIp,
                        unsigned short& fromPort, DWORD timeoutMs)
{
    if (m_socket == INVALID_SOCKET)
    {
        return -1;
    }

    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(m_socket, &readSet);

    timeval timeout;
    timeval* timeoutPtr = NULL;
    if (timeoutMs != INFINITE)
    {
        timeout.tv_sec = (long)(timeoutMs / 1000);
        timeout.tv_usec = (long)((timeoutMs % 1000) * 1000);
        timeoutPtr = &timeout;
    }

    int ready = select(0, &readSet, NULL, NULL, timeoutPtr);
    if (ready == 0)
    {
        return 0;
    }
    if (ready == SOCKET_ERROR)
    {
        return -1;
    }

    sockaddr_in source;
    int sourceLength = sizeof(source);
    ZeroMemory(&source, sizeof(source));
    int received = recvfrom(m_socket, (char*)buffer, capacity, 0,
                            (sockaddr*)&source, &sourceLength);
    if (received == SOCKET_ERROR)
    {
        return -1;
    }

    fromIp = inet_ntoa(source.sin_addr);
    fromPort = ntohs(source.sin_port);
    return received;
}

void UdpSocket::Close()
{
    if (m_socket != INVALID_SOCKET)
    {
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
    }
    m_port = 0;
}

}  // namespace lsxp
