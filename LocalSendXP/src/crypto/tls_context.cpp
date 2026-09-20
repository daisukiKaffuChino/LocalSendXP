#include "lsxp/tls.h"
#include "openssl_api.h"

#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/err.h>

namespace lsxp {

namespace {

// TLS 1.2 suites that the official (rustls based) LocalSend offers, plus the
// classic RSA/ECDHE fallbacks used by older peers.
const char* const kCipherList =
    "ECDHE-RSA-AES128-GCM-SHA256:"
    "ECDHE-RSA-AES256-GCM-SHA384:"
    "ECDHE-RSA-CHACHA20-POLY1305:"
    "ECDHE-RSA-AES128-SHA256:"
    "ECDHE-RSA-AES256-SHA384:"
    "ECDHE-RSA-AES128-SHA:"
    "ECDHE-RSA-AES256-SHA:"
    "AES128-GCM-SHA256:"
    "AES256-GCM-SHA384";

const long kCertificateDays = 3650;

bool ReadWholeFile(const std::wstring& path, std::string& out)
{
    out.clear();
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    char buffer[4096];
    for (;;)
    {
        DWORD read = 0;
        if (!ReadFile(file, buffer, sizeof(buffer), &read, NULL) || read == 0)
        {
            break;
        }
        out.append(buffer, (size_t)read);
    }
    CloseHandle(file);
    return !out.empty();
}

bool WriteWholeFile(const std::wstring& path, const std::string& data, std::string& errorText)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        errorText = Format("cannot create %s (%lu)", WideToUtf8(path).c_str(), GetLastError());
        return false;
    }
    DWORD written = 0;
    bool ok = WriteFile(file, data.data(), (DWORD)data.size(), &written, NULL) != FALSE &&
              written == (DWORD)data.size();
    CloseHandle(file);
    if (!ok)
    {
        errorText = Format("cannot write %s (%lu)", WideToUtf8(path).c_str(), GetLastError());
    }
    return ok;
}

std::string MemoryBioToString(bio_st* bio, const OpenSslApi& api)
{
    BUF_MEM* memory = NULL;
    api.BIO_ctrl(bio, BIO_C_GET_BUF_MEM_PTR, 0, &memory);
    if (memory == NULL || memory->data == NULL || memory->length == 0)
    {
        return std::string();
    }
    return std::string(memory->data, memory->length);
}

bool GenerateSelfSigned(std::string& pemOut, std::string& errorText)
{
    OpenSslApi& api = SslApi();
    bool ok = false;
    rsa_st* rsa = NULL;
    bignum_st* exponent = NULL;
    evp_pkey_st* key = NULL;
    x509_st* certificate = NULL;
    bio_st* bio = NULL;
    x509_name_st* name = NULL;
    void* notBefore = NULL;
    void* notAfter = NULL;
    std::string commonName;      // declared up front: goto must not skip it

    rsa = api.RSA_new();
    exponent = api.BN_new();
    if (rsa == NULL || exponent == NULL)
    {
        errorText = "cannot allocate RSA key";
        goto cleanup;
    }

    // 65537 as the public exponent (RSA_F4).
    if (api.BN_set_word(exponent, 65537) != 1 ||
        api.RSA_generate_key_ex(rsa, 2048, exponent, NULL) != 1)
    {
        errorText = "RSA key generation failed: " + TlsContext::Instance().LastOpenSslError("RSA_generate_key_ex");
        goto cleanup;
    }

    key = api.EVP_PKEY_new();
    if (key == NULL || api.EVP_PKEY_assign(key, EVP_PKEY_RSA, rsa) != 1)
    {
        errorText = "cannot wrap the RSA key";
        goto cleanup;
    }
    rsa = NULL;   // owned by the EVP key now

    certificate = api.X509_new();
    if (certificate == NULL)
    {
        errorText = "cannot allocate the certificate";
        goto cleanup;
    }

    api.X509_set_version(certificate, X509_VERSION_3);
    api.ASN1_INTEGER_set(api.X509_get_serialNumber(certificate),
                         (long)(GetTickCount() & 0x7FFFFFFF));
    // X509_get_notBefore/notAfter are macros in 1.0.2, so the validity period
    // is built from stand alone ASN1_TIME values and copied in.
    notBefore = api.X509_gmtime_adj(NULL, 0);
    notAfter = api.X509_gmtime_adj(NULL, 60L * 60L * 24L * kCertificateDays);
    if (notBefore == NULL || notAfter == NULL ||
        api.X509_set_notBefore(certificate, notBefore) != 1 ||
        api.X509_set_notAfter(certificate, notAfter) != 1)
    {
        errorText = "cannot set the certificate validity period";
        goto cleanup;
    }
    api.X509_set_pubkey(certificate, key);

    name = api.X509_get_subject_name(certificate);
    commonName = WideToAnsi(GetLocalComputerNameW());
    if (commonName.empty())
    {
        commonName = "LocalSendXP";
    }
    api.X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC,
                                   (const unsigned char*)"CN", -1, -1, 0);
    api.X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC,
                                   (const unsigned char*)"LocalSend XP", -1, -1, 0);
    api.X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                                   (const unsigned char*)commonName.c_str(), -1, -1, 0);
    api.X509_set_issuer_name(certificate, name);

    if (api.X509_sign(certificate, key, api.EVP_sha256()) <= 0)
    {
        errorText = "cannot sign the certificate: " + TlsContext::Instance().LastOpenSslError("X509_sign");
        goto cleanup;
    }

    bio = api.BIO_new(api.BIO_s_mem());
    if (bio == NULL)
    {
        errorText = "cannot allocate the PEM buffer";
        goto cleanup;
    }
    if (api.PEM_write_bio_X509(bio, certificate) != 1 ||
        api.PEM_write_bio_PrivateKey(bio, key, NULL, NULL, 0, NULL, NULL) != 1)
    {
        errorText = "cannot encode the certificate: " + TlsContext::Instance().LastOpenSslError("PEM_write");
        goto cleanup;
    }

    pemOut = MemoryBioToString(bio, api);
    ok = !pemOut.empty();
    if (!ok)
    {
        errorText = "certificate encoding produced no data";
    }

cleanup:
    if (notBefore != NULL)
    {
        api.ASN1_TIME_free(notBefore);
        notBefore = NULL;
    }
    if (notAfter != NULL)
    {
        api.ASN1_TIME_free(notAfter);
        notAfter = NULL;
    }
    if (bio != NULL)
    {
        api.BIO_free(bio);
    }
    if (certificate != NULL)
    {
        api.X509_free(certificate);
    }
    if (key != NULL)
    {
        api.EVP_PKEY_free(key);
    }
    if (rsa != NULL)
    {
        api.RSA_free(rsa);
    }
    if (exponent != NULL)
    {
        api.BN_free(exponent);
    }
    return ok;
}

bool ParsePem(const std::string& pem, x509_st** certificateOut, evp_pkey_st** keyOut,
              std::string& errorText)
{
    OpenSslApi& api = SslApi();
    *certificateOut = NULL;
    *keyOut = NULL;

    bio_st* bio = api.BIO_new_mem_buf((void*)pem.data(), (int)pem.size());
    if (bio == NULL)
    {
        errorText = "cannot read the PEM data";
        return false;
    }

    x509_st* certificate = api.PEM_read_bio_X509(bio, NULL, NULL, NULL);
    if (certificate == NULL)
    {
        api.BIO_free(bio);
        errorText = "the stored certificate is not a valid PEM file";
        return false;
    }

    // Rewind by creating a second memory BIO for the key.
    api.BIO_free(bio);
    bio = api.BIO_new_mem_buf((void*)pem.data(), (int)pem.size());
    evp_pkey_st* key = (bio != NULL) ? api.PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL) : NULL;
    if (bio != NULL)
    {
        api.BIO_free(bio);
    }
    if (key == NULL)
    {
        api.X509_free(certificate);
        errorText = "the stored private key is not a valid PEM file";
        return false;
    }

    *certificateOut = certificate;
    *keyOut = key;
    return true;
}

}  // namespace

TlsContext& TlsContext::Instance()
{
    static TlsContext instance;
    return instance;
}

TlsContext::TlsContext()
    : m_ready(false),
      m_hasCertificate(false),
      m_requireClientCertificate(false),
      m_clientContext(NULL),
      m_serverContext(NULL)
{
}

TlsContext::~TlsContext()
{
    Stop();
}

std::string TlsContext::LastOpenSslError(const char* what) const
{
    OpenSslApi& api = SslApi();
    std::string message = (what != NULL) ? what : "OpenSSL";

    unsigned long code = api.ERR_get_error();
    if (code != 0)
    {
        char buffer[256];
        buffer[0] = '\0';
        api.ERR_error_string_n(code, buffer, sizeof(buffer) - 1);
        buffer[sizeof(buffer) - 1] = '\0';
        message += ": ";
        message += buffer;
    }
    return message;
}

std::string TlsContext::FingerprintOfCertificate(void* certificate) const
{
    if (certificate == NULL)
    {
        return std::string();
    }

    OpenSslApi& api = SslApi();
    unsigned char digest[64];
    unsigned int length = 0;
    if (api.X509_digest((x509_st*)certificate, api.EVP_sha256(), digest, &length) != 1 || length == 0)
    {
        return std::string();
    }

    static const char* digits = "0123456789ABCDEF";
    std::string result;
    result.reserve(length * 2);
    for (unsigned int i = 0; i < length; ++i)
    {
        result += digits[(digest[i] >> 4) & 0x0F];
        result += digits[digest[i] & 0x0F];
    }
    return result;
}

bool TlsContext::Start(const std::wstring& certificatePath,
                       const std::wstring& caBundlePath,
                       bool requireClientCertificate,
                       bool allowLegacyTls,
                       std::string& errorText)
{
    if (m_ready)
    {
        return true;
    }

    OpenSslApi& api = SslApi();
    if (!api.Load())
    {
        m_error = api.LoadError();
        errorText = m_error;
        return false;
    }

    const char* version = api.SSLeay_version(SSLEAY_VERSION);
    m_version = (version != NULL) ? version : "OpenSSL 1.0.2";

    // 1) certificate: reuse the stored one, otherwise create a self signed one
    std::string pem;
    if (ReadWholeFile(certificatePath, pem))
    {
        m_hasCertificate = true;
    }
    else
    {
        LogLine("TLS: generating a self signed certificate (%s)", WideToUtf8(certificatePath).c_str());
        if (!GenerateSelfSigned(pem, errorText))
        {
            m_error = errorText;
            return false;
        }
        if (!WriteWholeFile(certificatePath, pem, errorText))
        {
            m_error = errorText;
            return false;
        }
        m_hasCertificate = true;
    }
    m_certificatePem = pem;

    x509_st* certificate = NULL;
    evp_pkey_st* key = NULL;
    if (!ParsePem(pem, &certificate, &key, errorText))
    {
        m_error = errorText;
        return false;
    }
    m_fingerprint = FingerprintOfCertificate(certificate);

    // 2) contexts
    unsigned long options = SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_COMPRESSION;
    if (!allowLegacyTls)
    {
        options |= SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1;   // TLS 1.2 only by default
    }

    m_clientContext = api.SSL_CTX_new(api.SSLv23_client_method());
    m_serverContext = api.SSL_CTX_new(api.SSLv23_server_method());
    if (m_clientContext == NULL || m_serverContext == NULL)
    {
        errorText = "cannot create the TLS contexts: " + LastOpenSslError("SSL_CTX_new");
        m_error = errorText;
        api.X509_free(certificate);
        api.EVP_PKEY_free(key);
        return false;
    }

    ssl_ctx_st* client = (ssl_ctx_st*)m_clientContext;
    ssl_ctx_st* server = (ssl_ctx_st*)m_serverContext;

    api.SSL_CTX_ctrl(client, SSL_CTRL_OPTIONS, (long)options, NULL);
    api.SSL_CTX_ctrl(server, SSL_CTRL_OPTIONS, (long)options, NULL);
    api.SSL_CTX_set_cipher_list(client, kCipherList);
    api.SSL_CTX_set_cipher_list(server, kCipherList);
    api.SSL_CTX_set_verify_depth(client, 9);
    api.SSL_CTX_set_verify_depth(server, 9);

    // Trust anchors for outgoing connections: our own CA bundle when present
    // (Windows XP store is too old to be relied on), otherwise the system one.
    if (!caBundlePath.empty() && FileExistsW(caBundlePath))
    {
        std::string ansiPath = WideToAnsi(caBundlePath);
        if (api.SSL_CTX_load_verify_locations(client, ansiPath.c_str(), NULL) != 1)
        {
            LogLine("TLS: cannot load CA bundle %s", ansiPath.c_str());
        }
    }
    else
    {
        api.SSL_CTX_set_default_verify_paths(client);
    }

    // Our identity: the server presents it, and the client presents it when a
    // peer asks for a client certificate (mutual TLS, as LocalSend uses).
    if (api.SSL_CTX_use_certificate(server, certificate) != 1 ||
        api.SSL_CTX_use_PrivateKey(server, key) != 1 ||
        api.SSL_CTX_use_certificate(client, certificate) != 1 ||
        api.SSL_CTX_use_PrivateKey(client, key) != 1)
    {
        errorText = "cannot install the certificate: " + LastOpenSslError("SSL_CTX_use_certificate");
        m_error = errorText;
        api.X509_free(certificate);
        api.EVP_PKEY_free(key);
        return false;
    }

    if (api.SSL_CTX_check_private_key(server) != 1)
    {
        errorText = "the certificate and the private key do not match";
        m_error = errorText;
        api.X509_free(certificate);
        api.EVP_PKEY_free(key);
        return false;
    }

    m_requireClientCertificate = requireClientCertificate;
    api.SSL_CTX_set_verify(server,
                           requireClientCertificate ? (SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT)
                                                    : SSL_VERIFY_NONE,
                           NULL);

    api.X509_free(certificate);
    api.EVP_PKEY_free(key);

    m_ready = true;
    LogLine("TLS ready: %s, fingerprint %s, TLS 1.2%s",
            m_version.c_str(), m_fingerprint.c_str(),
            allowLegacyTls ? " (+TLS1.0/1.1 fallback allowed)" : " only");
    return true;
}

void TlsContext::Stop()
{
    if (!m_ready)
    {
        return;
    }

    OpenSslApi& api = SslApi();
    if (m_clientContext != NULL)
    {
        api.SSL_CTX_free((ssl_ctx_st*)m_clientContext);
        m_clientContext = NULL;
    }
    if (m_serverContext != NULL)
    {
        api.SSL_CTX_free((ssl_ctx_st*)m_serverContext);
        m_serverContext = NULL;
    }
    api.Unload();
    m_ready = false;
}

bool TlsAvailable()
{
    std::wstring directory = GetModuleDirectoryW();
    return FileExistsW(JoinPathW(directory, L"libeay32.dll")) &&
           FileExistsW(JoinPathW(directory, L"ssleay32.dll"));
}

}  // namespace lsxp
