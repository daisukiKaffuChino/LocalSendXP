#include "openssl_api.h"

namespace lsxp {

namespace {

// OpenSSL 1.0.x needs the application to supply locking callbacks when it is
// used from more than one thread. This is the classic Win32 recipe: one
// CRITICAL_SECTION per "lock" reported by CRYPTO_num_locks().
CRITICAL_SECTION* g_locks = NULL;
int               g_lockCount = 0;

void OpenSslLockingCallback(int mode, int type, const char* file, int line)
{
    (void)file;
    (void)line;
    if (g_locks == NULL || type < 0 || type >= g_lockCount)
    {
        return;
    }
    if ((mode & 1) != 0)   // CRYPTO_LOCK
    {
        EnterCriticalSection(&g_locks[type]);
    }
    else
    {
        LeaveCriticalSection(&g_locks[type]);
    }
}

void OpenSslThreadIdCallback(crypto_threadid_st* id)
{
    SslApi().CRYPTO_THREADID_set_numeric(id, (unsigned long)GetCurrentThreadId());
}

template <typename T>
bool BindSymbol(HMODULE crypto, HMODULE ssl, const char* name, T& target)
{
    FARPROC address = GetProcAddress(crypto, name);
    if (address == NULL && ssl != NULL)
    {
        address = GetProcAddress(ssl, name);
    }
    if (address == NULL)
    {
        return false;
    }
    target = reinterpret_cast<T>(address);
    return true;
}

HMODULE LoadFromAppDirectory(const wchar_t* fileName)
{
    std::wstring full = JoinPathW(GetModuleDirectoryW(), fileName);
    HMODULE module = LoadLibraryW(full.c_str());
    if (module == NULL)
    {
        module = LoadLibraryW(fileName);   // fall back to the default search order
    }
    return module;
}

}  // namespace

OpenSslApi::OpenSslApi()
    : m_loaded(false),
      m_crypto(NULL),
      m_ssl(NULL)
{
    // All entry points are plain function pointers declared consecutively, so
    // clearing the range spanning them is enough (m_loaded/m_crypto/m_ssl and
    // m_loadError are initialised by the member list above).
    ZeroMemory(&SSL_library_init,
               (char*)&ERR_remove_state - (char*)&SSL_library_init + sizeof(ERR_remove_state));
}

OpenSslApi& SslApi()
{
    static OpenSslApi instance;
    return instance;
}

bool OpenSslApi::Load()
{
    if (m_loaded)
    {
        return true;
    }

    m_crypto = LoadFromAppDirectory(L"libeay32.dll");
    if (m_crypto == NULL)
    {
        m_loadError = "libeay32.dll (OpenSSL 1.0.2) is missing";
        return false;
    }
    m_ssl = LoadFromAppDirectory(L"ssleay32.dll");
    if (m_ssl == NULL)
    {
        m_loadError = "ssleay32.dll (OpenSSL 1.0.2) is missing";
        FreeLibrary(m_crypto);
        m_crypto = NULL;
        return false;
    }

    HMODULE crypto = m_crypto;
    HMODULE ssl = m_ssl;
    std::string missing;

#define LSXP_BIND(field)                                                     \
    if (!BindSymbol(crypto, ssl, #field, field))                             \
    {                                                                        \
        if (!missing.empty())                                                \
        {                                                                    \
            missing += ", ";                                                 \
        }                                                                    \
        missing += #field;                                                   \
    }

    // library
    LSXP_BIND(SSL_library_init)
    LSXP_BIND(SSL_load_error_strings)
    LSXP_BIND(OPENSSL_add_all_algorithms_conf)
    LSXP_BIND(ERR_load_crypto_strings)
    LSXP_BIND(ERR_get_error)
    LSXP_BIND(ERR_error_string_n)
    LSXP_BIND(ERR_clear_error)
    LSXP_BIND(SSLeay_version)
    LSXP_BIND(ERR_remove_state)
    LSXP_BIND(CRYPTO_num_locks)
    LSXP_BIND(CRYPTO_set_locking_callback)
    LSXP_BIND(CRYPTO_THREADID_set_callback)
    LSXP_BIND(CRYPTO_THREADID_set_numeric)
    LSXP_BIND(CRYPTO_cleanup_all_ex_data)
    LSXP_BIND(EVP_cleanup)
    LSXP_BIND(RAND_bytes)

    // contexts / methods
    LSXP_BIND(SSL_CTX_new)
    LSXP_BIND(SSL_CTX_free)
    LSXP_BIND(TLSv1_2_client_method)
    LSXP_BIND(TLSv1_2_server_method)
    LSXP_BIND(TLSv1_client_method)
    LSXP_BIND(TLSv1_server_method)
    LSXP_BIND(SSLv23_client_method)
    LSXP_BIND(SSLv23_server_method)
    LSXP_BIND(SSL_CTX_set_verify)
    LSXP_BIND(SSL_CTX_set_verify_depth)
    LSXP_BIND(SSL_CTX_use_certificate_file)
    LSXP_BIND(SSL_CTX_use_PrivateKey_file)
    LSXP_BIND(SSL_CTX_use_certificate)
    LSXP_BIND(SSL_CTX_use_PrivateKey)
    LSXP_BIND(SSL_CTX_check_private_key)
    LSXP_BIND(SSL_CTX_set_cipher_list)
    LSXP_BIND(SSL_CTX_load_verify_locations)
    LSXP_BIND(SSL_CTX_set_default_verify_paths)
    LSXP_BIND(SSL_CTX_ctrl)

    // connections
    LSXP_BIND(SSL_new)
    LSXP_BIND(SSL_free)
    LSXP_BIND(SSL_set_fd)
    LSXP_BIND(SSL_connect)
    LSXP_BIND(SSL_accept)
    LSXP_BIND(SSL_read)
    LSXP_BIND(SSL_write)
    LSXP_BIND(SSL_shutdown)
    LSXP_BIND(SSL_get_error)
    LSXP_BIND(SSL_get_verify_result)
    LSXP_BIND(SSL_pending)
    LSXP_BIND(SSL_set_verify)
    LSXP_BIND(SSL_ctrl)
    LSXP_BIND(SSL_get_version)
    LSXP_BIND(SSL_get_current_cipher)
    LSXP_BIND(SSL_CIPHER_get_name)
    LSXP_BIND(SSL_get_peer_certificate)
    LSXP_BIND(SSL_get_certificate)

    // certificates and keys
    LSXP_BIND(X509_free)
    LSXP_BIND(X509_digest)
    LSXP_BIND(X509_get_subject_name)
    LSXP_BIND(X509_NAME_oneline)
    LSXP_BIND(X509_verify_cert_error_string)
    LSXP_BIND(X509_check_host)
    LSXP_BIND(X509_new)
    LSXP_BIND(X509_set_version)
    LSXP_BIND(X509_set_serialNumber)
    LSXP_BIND(X509_get_serialNumber)
    LSXP_BIND(X509_set_issuer_name)
    LSXP_BIND(X509_set_subject_name)
    LSXP_BIND(X509_get_issuer_name)
    LSXP_BIND(X509_NAME_add_entry_by_txt)
    LSXP_BIND(X509_set_pubkey)
    LSXP_BIND(X509_sign)
    LSXP_BIND(X509_set_notBefore)
    LSXP_BIND(X509_set_notAfter)
    LSXP_BIND(X509_gmtime_adj)
    LSXP_BIND(X509_get_ext_by_NID)

    // evp / rsa / bn / bio / pem
    LSXP_BIND(EVP_sha256)
    LSXP_BIND(EVP_PKEY_new)
    LSXP_BIND(EVP_PKEY_free)
    LSXP_BIND(EVP_PKEY_assign)
    LSXP_BIND(EVP_PKEY_get1_RSA)
    LSXP_BIND(EVP_PKEY_set1_RSA)
    LSXP_BIND(RSA_new)
    LSXP_BIND(RSA_free)
    LSXP_BIND(RSA_generate_key_ex)
    LSXP_BIND(BN_new)
    LSXP_BIND(BN_free)
    LSXP_BIND(BN_set_word)
    LSXP_BIND(BN_num_bits)
    LSXP_BIND(BIO_new_file)
    LSXP_BIND(BIO_new_mem_buf)
    LSXP_BIND(BIO_new)
    LSXP_BIND(BIO_s_mem)
    LSXP_BIND(BIO_free)
    LSXP_BIND(BIO_free_all)
    LSXP_BIND(BIO_ctrl)
    LSXP_BIND(PEM_read_bio_X509)
    LSXP_BIND(PEM_read_bio_PrivateKey)
    LSXP_BIND(PEM_write_bio_X509)
    LSXP_BIND(PEM_write_bio_PrivateKey)
    LSXP_BIND(PEM_write_bio_RSA_PUBKEY)
    LSXP_BIND(ASN1_INTEGER_set)
    LSXP_BIND(ASN1_TIME_free)

#undef LSXP_BIND

    if (!missing.empty())
    {
        m_loadError = "OpenSSL entry points missing: " + missing;
        FreeLibrary(m_ssl);
        FreeLibrary(m_crypto);
        m_ssl = NULL;
        m_crypto = NULL;
        return false;
    }

    SSL_library_init();
    SSL_load_error_strings();
    OPENSSL_add_all_algorithms_conf();

    g_lockCount = CRYPTO_num_locks();
    if (g_lockCount > 0)
    {
        g_locks = new CRITICAL_SECTION[g_lockCount];
        for (int i = 0; i < g_lockCount; ++i)
        {
            InitializeCriticalSection(&g_locks[i]);
        }
        CRYPTO_set_locking_callback(&OpenSslLockingCallback);
        CRYPTO_THREADID_set_callback(&OpenSslThreadIdCallback);
    }

    m_loaded = true;
    LogLine("OpenSSL loaded: %s / %s (%d locks)",
            WideToUtf8(JoinPathW(GetModuleDirectoryW(), L"libeay32.dll")).c_str(),
            WideToUtf8(JoinPathW(GetModuleDirectoryW(), L"ssleay32.dll")).c_str(),
            g_lockCount);
    return true;
}

void OpenSslApi::Unload()
{
    if (!m_loaded)
    {
        return;
    }

    if (ERR_remove_state != NULL)
    {
        ERR_remove_state(0);
    }
    if (EVP_cleanup != NULL)
    {
        EVP_cleanup();
    }
    if (CRYPTO_cleanup_all_ex_data != NULL)
    {
        CRYPTO_cleanup_all_ex_data();
    }
    if (CRYPTO_set_locking_callback != NULL)
    {
        CRYPTO_set_locking_callback(NULL);
    }
    if (CRYPTO_THREADID_set_callback != NULL)
    {
        CRYPTO_THREADID_set_callback(NULL);
    }

    if (g_locks != NULL)
    {
        for (int i = 0; i < g_lockCount; ++i)
        {
            DeleteCriticalSection(&g_locks[i]);
        }
        delete[] g_locks;
        g_locks = NULL;
        g_lockCount = 0;
    }

    // Leave the DLLs loaded: other threads may still be shutting down.
    m_loaded = false;
    LogLine("OpenSSL released");
}

}  // namespace lsxp
