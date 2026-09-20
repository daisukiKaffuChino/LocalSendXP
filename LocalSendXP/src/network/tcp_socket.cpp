#include "lsxp/network.h"

namespace lsxp {

namespace {

bool WaitSocket(SOCKET handle, bool forWrite, DWORD timeoutMs)
{
    fd_set readSet;
    fd_set writeSet;
    fd_set errorSet;
    FD_ZERO(&readSet);
    FD_ZERO(&writeSet);
    FD_ZERO(&errorSet);

    if (forWrite)
    {
        FD_SET(handle, &writeSet);
    }
    else
    {
        FD_SET(handle, &readSet);
    }
    FD_SET(handle, &errorSet);

    timeval timeout;
    timeout.tv_sec = (long)(timeoutMs / 1000);
    timeout.tv_usec = (long)((timeoutMs % 1000) * 1000);

    int ready = select(0, &readSet, &writeSet, &errorSet, &timeout);
    if (ready <= 0)
    {
        return false;
    }
    if (FD_ISSET(handle, &errorSet))
    {
        return false;
    }
    return true;
}

}  // namespace

TcpSocket::TcpSocket()
    : m_socket(INVALID_SOCKET), m_peerPort(0)
{
}

TcpSocket::~TcpSocket()
{
    Close();
}

bool TcpSocket::Attach(SOCKET handle)
{
    Close();
    if (handle == INVALID_SOCKET)
    {
        return false;
    }
    m_socket = handle;
    return true;
}

void TcpSocket::SetPeer(const std::string& ip, unsigned short port)
{
    m_peerIp = ip;
    m_peerPort = port;
}

void TcpSocket::SetNoDelay(bool enable)
{
    if (m_socket == INVALID_SOCKET)
    {
        return;
    }
    BOOL value = enable ? TRUE : FALSE;
    setsockopt(m_socket, IPPROTO_TCP, TCP_NODELAY, (const char*)&value, sizeof(value));
}

bool TcpSocket::Connect(const std::string& ip, unsigned short port,
                        DWORD timeoutMs, std::string& errorText)
{
    Close();

    m_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_socket == INVALID_SOCKET)
    {
        errorText = Format("socket() failed (%d)", WSAGetLastError());
        return false;
    }
    SetNoDelay(true);

    sockaddr_in target;
    ZeroMemory(&target, sizeof(target));
    target.sin_family = AF_INET;
    target.sin_port = htons(port);
    target.sin_addr.s_addr = inet_addr(ip.c_str());
    if (target.sin_addr.s_addr == INADDR_NONE)
    {
        errorText = "invalid address " + ip;
        Close();
        return false;
    }

    // Non blocking connect + select: the Windows 95/98/2000/XP way.
    u_long nonBlocking = 1;
    ioctlsocket(m_socket, FIONBIO, &nonBlocking);

    int result = connect(m_socket, (sockaddr*)&target, sizeof(target));
    if (result == SOCKET_ERROR)
    {
        int error = WSAGetLastError();
        if (error != WSAEWOULDBLOCK && error != WSAEINPROGRESS)
        {
            errorText = Format("connect(%s:%u) failed (%d)", ip.c_str(), (unsigned)port, error);
            Close();
            return false;
        }

        if (!WaitSocket(m_socket, true, timeoutMs))
        {
            errorText = Format("connect(%s:%u) timed out", ip.c_str(), (unsigned)port);
            Close();
            return false;
        }

        int socketError = 0;
        int length = sizeof(socketError);
        if (getsockopt(m_socket, SOL_SOCKET, SO_ERROR, (char*)&socketError, &length) == SOCKET_ERROR)
        {
            errorText = Format("getsockopt failed (%d)", WSAGetLastError());
            Close();
            return false;
        }
        if (socketError != 0)
        {
            errorText = Format("connect(%s:%u) failed (%d)", ip.c_str(), (unsigned)port, socketError);
            Close();
            return false;
        }
    }

    nonBlocking = 0;
    ioctlsocket(m_socket, FIONBIO, &nonBlocking);

    m_peerIp = ip;
    m_peerPort = port;
    return true;
}

bool TcpSocket::SendAll(const void* data, int length, std::string& errorText)
{
    if (m_socket == INVALID_SOCKET)
    {
        errorText = "socket is not connected";
        return false;
    }

    const char* cursor = (const char*)data;
    int remaining = length;
    while (remaining > 0)
    {
        int sent = send(m_socket, cursor, remaining, 0);
        if (sent == SOCKET_ERROR)
        {
            int error = WSAGetLastError();
            if (error == WSAEWOULDBLOCK)
            {
                if (!WaitSocket(m_socket, true, 30000))
                {
                    errorText = "send timed out";
                    return false;
                }
                continue;
            }
            errorText = Format("send failed (%d)", error);
            return false;
        }
        cursor += sent;
        remaining -= sent;
    }
    return true;
}

int TcpSocket::Recv(void* buffer, int capacity, DWORD timeoutMs)
{
    if (m_socket == INVALID_SOCKET)
    {
        return -1;
    }
    if (!WaitSocket(m_socket, false, timeoutMs))
    {
        return -1;
    }

    int received = recv(m_socket, (char*)buffer, capacity, 0);
    if (received == SOCKET_ERROR)
    {
        return -1;
    }
    return received;
}

void TcpSocket::Close()
{
    if (m_socket != INVALID_SOCKET)
    {
        shutdown(m_socket, SD_BOTH);
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
    }
    m_peerIp.clear();
    m_peerPort = 0;
}

TcpListener::TcpListener()
    : m_socket(INVALID_SOCKET), m_port(0), m_stop(0)
{
}

TcpListener::~TcpListener()
{
    Close();
}

bool TcpListener::Listen(unsigned short port, const std::string& bindIp, std::string& errorText)
{
    Close();
    m_stop = 0;

    m_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_socket == INVALID_SOCKET)
    {
        errorText = Format("socket() failed (%d)", WSAGetLastError());
        return false;
    }

    // Deliberately no SO_REUSEADDR: on Windows it would allow another
    // process to steal our listening port.
    sockaddr_in address;
    ZeroMemory(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = (bindIp.empty() || bindIp == "0.0.0.0")
                              ? htonl(INADDR_ANY)
                              : inet_addr(bindIp.c_str());

    if (bind(m_socket, (sockaddr*)&address, sizeof(address)) == SOCKET_ERROR)
    {
        int error = WSAGetLastError();
        if (error == WSAEADDRINUSE)
        {
            errorText = Format("port %u is already in use", (unsigned)port);
        }
        else
        {
            errorText = Format("bind() on port %u failed (%d)", (unsigned)port, error);
        }
        Close();
        return false;
    }

    if (listen(m_socket, 64) == SOCKET_ERROR)
    {
        errorText = Format("listen() failed (%d)", WSAGetLastError());
        Close();
        return false;
    }

    m_port = port;
    return true;
}

SOCKET TcpListener::Accept(DWORD timeoutMs, std::string& peerIp, unsigned short& peerPort)
{
    if (m_socket == INVALID_SOCKET || m_stop)
    {
        return INVALID_SOCKET;
    }
    if (!WaitSocket(m_socket, false, timeoutMs))
    {
        return INVALID_SOCKET;
    }
    if (m_stop)
    {
        return INVALID_SOCKET;
    }

    sockaddr_in peer;
    int peerLength = sizeof(peer);
    ZeroMemory(&peer, sizeof(peer));
    SOCKET accepted = accept(m_socket, (sockaddr*)&peer, &peerLength);
    if (accepted == INVALID_SOCKET)
    {
        return INVALID_SOCKET;
    }

    peerIp = inet_ntoa(peer.sin_addr);
    peerPort = ntohs(peer.sin_port);
    return accepted;
}

void TcpListener::Close()
{
    m_stop = 1;
    if (m_socket != INVALID_SOCKET)
    {
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
    }
    m_port = 0;
}

}  // namespace lsxp
