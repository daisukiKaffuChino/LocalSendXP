#ifndef LSXP_TLS_H
#define LSXP_TLS_H

// Public TLS facade. No OpenSSL declaration appears here: the implementation
// lives in src/crypto (dynamic loading) and src/network/tls_socket.cpp, so the
// HTTP and protocol layers stay independent of the TLS library.

#include "common.h"
#include "network.h"

namespace lsxp {

// How a peer certificate is checked.
enum TlsVerifyMode
{
    // "Allow insecure HTTPS" opt-in (config). Nothing is verified.
    TLS_VERIFY_ALLOW_INSECURE = 0,
    // Standard PKIX validation against a CA bundle plus hostname check.
    TLS_VERIFY_CA = 1,
    // LocalSend trust model: the leaf certificate must hash to the fingerprint
    // that the peer announced over UDP.
    TLS_VERIFY_FINGERPRINT = 2
};

// Owns the OpenSSL library state and the client/server contexts. Created once
// at start-up, released at shut-down.
class TlsContext
{
public:
    static TlsContext& Instance();

    bool Start(const std::wstring& certificatePath, const std::wstring& caBundlePath,
               bool requireClientCertificate, bool allowLegacyTls, std::string& errorText);
    void Stop();

    bool IsReady() const { return m_ready; }
    const std::string& Error() const { return m_error; }
    const std::string& Version() const { return m_version; }

    // SHA-256 of our own certificate (upper case hex) - the LocalSend
    // "fingerprint" in HTTPS mode.
    const std::string& Fingerprint() const { return m_fingerprint; }
    bool HasCertificate() const { return m_hasCertificate; }

    void* ClientHandle() const { return m_clientContext; }
    void* ServerHandle() const { return m_serverContext; }

    // Helpers used by the TLS stream.
    std::string FingerprintOfCertificate(void* certificate) const;
    std::string LastOpenSslError(const char* what) const;

private:
    TlsContext();
    ~TlsContext();

    bool EnsureCertificate(const std::wstring& path, std::string& errorText);
    bool LoadCertificateFile(const std::wstring& path, std::string& errorText);

    bool        m_ready;
    bool        m_hasCertificate;
    bool        m_requireClientCertificate;
    void*       m_clientContext;   // SSL_CTX*
    void*       m_serverContext;   // SSL_CTX*
    std::string m_error;
    std::string m_version;
    std::string m_fingerprint;
    std::string m_certificatePem;
};

// A TLS session over an existing TCP socket.
class TlsStream : public IStream
{
public:
    TlsStream();
    virtual ~TlsStream();

    // Client side: hostName is used for SNI and (in CA mode) for the name check.
    bool Connect(TcpSocket* socket, const std::string& hostName,
                 TlsVerifyMode mode, const std::string& expectedFingerprint,
                 bool allowLegacyTls, std::string& errorText);

    // Server side.
    bool Accept(TcpSocket* socket, bool allowLegacyTls, std::string& errorText);

    virtual bool SendAll(const void* data, int length, std::string& errorText);
    virtual int  Recv(void* buffer, int capacity, DWORD timeoutMs);
    virtual void Close();
    virtual bool IsOpen() const { return m_ssl != NULL; }
    virtual std::string PeerIp() const { return m_socket != NULL ? m_socket->PeerIp() : std::string(); }
    virtual unsigned short PeerPort() const { return m_socket != NULL ? m_socket->PeerPort() : 0; }

    const std::string& ProtocolVersion() const { return m_protocolVersion; }
    const std::string& CipherName() const { return m_cipherName; }
    // Fingerprint of the certificate the peer presented (empty when none).
    const std::string& PeerFingerprint() const { return m_peerFingerprint; }

private:
    void*       m_ssl;             // SSL*
    TcpSocket*  m_socket;
    bool        m_ownsSocket;
    std::string m_protocolVersion;
    std::string m_cipherName;
    std::string m_peerFingerprint;
};

// True when the OpenSSL DLLs are present next to the executable.
bool TlsAvailable();

}  // namespace lsxp

#endif  // LSXP_TLS_H
