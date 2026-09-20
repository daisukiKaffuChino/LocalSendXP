#ifndef LSXP_NETWORK_H
#define LSXP_NETWORK_H

#include "common.h"

namespace lsxp {

bool WinsockStartup(std::string& errorText);
void WinsockCleanup();

// Byte stream abstraction. The HTTP layer talks to this interface only, so it
// never has to know whether the bytes travel over plain TCP or through TLS.
class IStream
{
public:
    virtual ~IStream() {}
    virtual bool SendAll(const void* data, int length, std::string& errorText) = 0;
    virtual int  Recv(void* buffer, int capacity, DWORD timeoutMs) = 0;
    virtual void Close() = 0;
    virtual bool IsOpen() const = 0;
    virtual std::string PeerIp() const = 0;
    virtual unsigned short PeerPort() const = 0;
};

class UdpSocket
{
public:
    UdpSocket();
    ~UdpSocket();

    bool Open(unsigned short port, bool reuseAddress, std::string& errorText);
    bool JoinMulticast(const std::string& group, std::string& errorText);
    void LeaveMulticast(const std::string& group);
    void SetBroadcast(bool enable);
    void SetMulticastLoop(bool enable);
    void SetMulticastTtl(int ttl);

    bool SendTo(const std::string& ip, unsigned short port, const void* data, int length, std::string& errorText);
    // Returns the number of bytes received, 0 on timeout, -1 on error.
    int  RecvFrom(void* buffer, int capacity, std::string& fromIp, unsigned short& fromPort, DWORD timeoutMs);

    void Close();
    bool IsOpen() const { return m_socket != INVALID_SOCKET; }
    unsigned short BoundPort() const { return m_port; }

private:
    SOCKET         m_socket;
    unsigned short m_port;
};

class TcpSocket : public IStream
{
public:
    TcpSocket();
    virtual ~TcpSocket();

    bool Connect(const std::string& ip, unsigned short port, DWORD timeoutMs, std::string& errorText);
    bool Attach(SOCKET handle);
    virtual bool SendAll(const void* data, int length, std::string& errorText);
    // Returns bytes read, 0 when the peer closed the connection, -1 on error.
    virtual int  Recv(void* buffer, int capacity, DWORD timeoutMs);
    virtual void Close();
    virtual bool IsOpen() const { return m_socket != INVALID_SOCKET; }
    virtual std::string PeerIp() const { return m_peerIp; }
    virtual unsigned short PeerPort() const { return m_peerPort; }

    void SetNoDelay(bool enable);
    SOCKET Handle() const { return m_socket; }
    void SetPeer(const std::string& ip, unsigned short port);

private:
    SOCKET         m_socket;
    std::string    m_peerIp;
    unsigned short m_peerPort;
};

class TcpListener
{
public:
    TcpListener();
    ~TcpListener();

    bool Listen(unsigned short port, const std::string& bindIp, std::string& errorText);
    SOCKET Accept(DWORD timeoutMs, std::string& peerIp, unsigned short& peerPort);
    void Close();
    bool IsOpen() const { return m_socket != INVALID_SOCKET; }
    unsigned short BoundPort() const { return m_port; }
    void RequestStop() { InterlockedExchange(&m_stop, 1); }

private:
    SOCKET         m_socket;
    unsigned short m_port;
    volatile LONG  m_stop;
};

}  // namespace lsxp

#endif  // LSXP_NETWORK_H
