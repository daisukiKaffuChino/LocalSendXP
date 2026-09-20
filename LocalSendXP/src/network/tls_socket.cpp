#include "lsxp/tls.h"
#include "../crypto/openssl_api.h"

#include <openssl/ssl.h>
#include <openssl/x509.h>

namespace lsxp {

TlsStream::TlsStream()
    : m_ssl(NULL),
      m_socket(NULL),
      m_ownsSocket(false)
{
}

TlsStream::~TlsStream()
{
    Close();
}

bool TlsStream::Connect(TcpSocket* socket,
                        const std::string& hostName,
                        TlsVerifyMode mode,
                        const std::string& expectedFingerprint,
                        bool allowLegacyTls,
                        std::string& errorText)
{
    (void)allowLegacyTls;   // the allowed protocol versions live in the context

    TlsContext& context = TlsContext::Instance();
    if (!context.IsReady())
    {
        errorText = "TLS is not available";
        return false;
    }
    if (socket == NULL || !socket->IsOpen())
    {
        errorText = "no TCP connection";
        return false;
    }

    OpenSslApi& api = SslApi();
    m_socket = socket;
    m_ssl = api.SSL_new((ssl_ctx_st*)context.ClientHandle());
    if (m_ssl == NULL)
    {
        errorText = "cannot create a TLS session";
        return false;
    }

    api.SSL_set_fd((ssl_st*)m_ssl, (int)socket->Handle());

    // Server Name Indication: needed both by real HTTPS servers and by any
    // peer that hosts several certificates.
    if (!hostName.empty())
    {
        api.SSL_ctrl((ssl_st*)m_ssl, SSL_CTRL_SET_TLSEXT_HOSTNAME,
                     TLSEXT_NAMETYPE_host_name, (void*)hostName.c_str());
    }

    // Chain validation happens inside the handshake only in CA mode; the
    // fingerprint mode compares the pinned hash right after the handshake.
    api.SSL_set_verify((ssl_st*)m_ssl,
                       (mode == TLS_VERIFY_CA) ? SSL_VERIFY_PEER : SSL_VERIFY_NONE,
                       NULL);

    int result = api.SSL_connect((ssl_st*)m_ssl);
    if (result != 1)
    {
        int error = api.SSL_get_error((ssl_st*)m_ssl, result);
        errorText = "HTTPS handshake failed";
        if (error == SSL_ERROR_SSL)
        {
            errorText += ": " + context.LastOpenSslError("SSL_connect");
        }
        Close();
        return false;
    }

    const char* version = api.SSL_get_version((ssl_st*)m_ssl);
    const ssl_cipher_st* cipher = api.SSL_get_current_cipher((ssl_st*)m_ssl);
    m_protocolVersion = (version != NULL) ? version : "";
    m_cipherName = (cipher != NULL) ? api.SSL_CIPHER_get_name(cipher) : "";

    x509_st* peer = api.SSL_get_peer_certificate((ssl_st*)m_ssl);
    if (peer != NULL)
    {
        m_peerFingerprint = context.FingerprintOfCertificate(peer);
    }

    bool verified = true;
    if (mode == TLS_VERIFY_FINGERPRINT)
    {
        if (peer == NULL || m_peerFingerprint.empty())
        {
            errorText = "HTTPS certificate verification failed: the peer sent no certificate";
            verified = false;
        }
        else if (!EqualsNoCase(m_peerFingerprint, expectedFingerprint))
        {
            errorText = "HTTPS certificate verification failed: "
                        "the certificate fingerprint does not match the announced value";
            verified = false;
        }
    }
    else if (mode == TLS_VERIFY_CA)
    {
        long verifyResult = api.SSL_get_verify_result((ssl_st*)m_ssl);
        if (verifyResult != 0)
        {
            errorText = std::string("HTTPS certificate verification failed: ") +
                        api.X509_verify_cert_error_string(verifyResult);
            verified = false;
        }
        else if (peer != NULL && !hostName.empty())
        {
            if (api.X509_check_host(peer, hostName.c_str(), hostName.size(), 0, NULL) != 1)
            {
                errorText = "HTTPS certificate verification failed: "
                            "the certificate does not match " + hostName;
                verified = false;
            }
        }
    }
    else if (mode == TLS_VERIFY_ALLOW_INSECURE)
    {
        LogLine("HTTPS: insecure mode, the certificate of %s was NOT verified", hostName.c_str());
    }

    if (peer != NULL)
    {
        api.X509_free(peer);
    }

    if (!verified)
    {
        Close();
        return false;
    }

    LogLine("HTTPS connected to %s: %s / %s (peer certificate %s)",
            hostName.c_str(), m_protocolVersion.c_str(), m_cipherName.c_str(),
            m_peerFingerprint.empty() ? "-" : m_peerFingerprint.c_str());
    return true;
}

bool TlsStream::Accept(TcpSocket* socket, bool allowLegacyTls, std::string& errorText)
{
    (void)allowLegacyTls;

    TlsContext& context = TlsContext::Instance();
    if (!context.IsReady())
    {
        errorText = "TLS is not available";
        return false;
    }
    if (socket == NULL || !socket->IsOpen())
    {
        errorText = "no TCP connection";
        return false;
    }

    OpenSslApi& api = SslApi();
    m_socket = socket;
    m_ssl = api.SSL_new((ssl_ctx_st*)context.ServerHandle());
    if (m_ssl == NULL)
    {
        errorText = "cannot create a TLS session";
        return false;
    }

    api.SSL_set_fd((ssl_st*)m_ssl, (int)socket->Handle());

    // The client certificate is checked at the protocol layer: the sender
    // declares its fingerprint in the JSON body, which we compare with the
    // certificate that was presented here.
    int result = api.SSL_accept((ssl_st*)m_ssl);
    if (result != 1)
    {
        int error = api.SSL_get_error((ssl_st*)m_ssl, result);
        errorText = "HTTPS handshake failed";
        if (error == SSL_ERROR_SSL)
        {
            errorText += ": " + context.LastOpenSslError("SSL_accept");
        }
        Close();
        return false;
    }

    const char* version = api.SSL_get_version((ssl_st*)m_ssl);
    const ssl_cipher_st* cipher = api.SSL_get_current_cipher((ssl_st*)m_ssl);
    m_protocolVersion = (version != NULL) ? version : "";
    m_cipherName = (cipher != NULL) ? api.SSL_CIPHER_get_name(cipher) : "";

    x509_st* peer = api.SSL_get_peer_certificate((ssl_st*)m_ssl);
    if (peer != NULL)
    {
        m_peerFingerprint = context.FingerprintOfCertificate(peer);
        api.X509_free(peer);
    }

    LogLine("HTTPS accepted from %s: %s / %s (client certificate %s)",
            socket->PeerIp().c_str(), m_protocolVersion.c_str(), m_cipherName.c_str(),
            m_peerFingerprint.empty() ? "none" : m_peerFingerprint.c_str());
    return true;
}

bool TlsStream::SendAll(const void* data, int length, std::string& errorText)
{
    if (m_ssl == NULL)
    {
        errorText = "no TLS session";
        return false;
    }

    OpenSslApi& api = SslApi();
    const char* cursor = (const char*)data;
    int remaining = length;

    while (remaining > 0)
    {
        int written = api.SSL_write((ssl_st*)m_ssl, cursor, remaining);
        if (written > 0)
        {
            cursor += written;
            remaining -= written;
            continue;
        }

        int error = api.SSL_get_error((ssl_st*)m_ssl, written);
        if (error == SSL_ERROR_WANT_WRITE || error == SSL_ERROR_WANT_READ)
        {
            // Wait until the socket can take more data.
            SOCKET handle = m_socket->Handle();
            fd_set writeSet;
            FD_ZERO(&writeSet);
            FD_SET(handle, &writeSet);
            timeval timeout;
            timeout.tv_sec = 30;
            timeout.tv_usec = 0;
            if (select(0, NULL, &writeSet, NULL, &timeout) <= 0)
            {
                errorText = "TLS write timed out";
                return false;
            }
            continue;
        }

        errorText = "TLS write failed: " + TlsContext::Instance().LastOpenSslError("SSL_write");
        return false;
    }
    return true;
}

int TlsStream::Recv(void* buffer, int capacity, DWORD timeoutMs)
{
    if (m_ssl == NULL || m_socket == NULL)
    {
        return -1;
    }

    OpenSslApi& api = SslApi();
    DWORD start = GetTickCount();

    for (;;)
    {
        if (api.SSL_pending((ssl_st*)m_ssl) > 0)
        {
            int received = api.SSL_read((ssl_st*)m_ssl, buffer, capacity);
            if (received > 0)
            {
                return received;
            }
            int error = api.SSL_get_error((ssl_st*)m_ssl, received);
            if (error == SSL_ERROR_ZERO_RETURN)
            {
                return 0;
            }
            if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE)
            {
                return -1;
            }
        }

        DWORD elapsed = GetTickCount() - start;
        if (elapsed >= timeoutMs)
        {
            return -1;
        }

        SOCKET handle = m_socket->Handle();
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(handle, &readSet);
        DWORD remaining = timeoutMs - elapsed;
        timeval timeout;
        timeout.tv_sec = (long)(remaining / 1000);
        timeout.tv_usec = (long)((remaining % 1000) * 1000);
        if (select(0, &readSet, NULL, NULL, &timeout) <= 0)
        {
            return -1;
        }

        // The socket has data: let OpenSSL read and decrypt the record. This
        // call is what actually fills SSL_pending, so it must not be skipped.
        int received = api.SSL_read((ssl_st*)m_ssl, buffer, capacity);
        if (received > 0)
        {
            return received;
        }
        int error = api.SSL_get_error((ssl_st*)m_ssl, received);
        if (error == SSL_ERROR_ZERO_RETURN)
        {
            return 0;
        }
        if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE)
        {
            return -1;
        }
    }
}

void TlsStream::Close()
{
    if (m_ssl != NULL)
    {
        OpenSslApi& api = SslApi();
        api.SSL_shutdown((ssl_st*)m_ssl);   // best effort, do not wait for the peer
        api.SSL_free((ssl_st*)m_ssl);
        m_ssl = NULL;
    }
    // The underlying TCP socket belongs to the caller.
    m_socket = NULL;
}

}  // namespace lsxp
