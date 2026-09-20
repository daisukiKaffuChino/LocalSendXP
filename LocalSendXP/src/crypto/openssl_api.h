#ifndef LSXP_OPENSSL_API_H
#define LSXP_OPENSSL_API_H

// Private header: only the crypto/network TLS sources may include this file.
// It declares the dynamically resolved OpenSSL 1.0.2 entry points so that no
// other part of the program has to see an OpenSSL declaration.

#include "lsxp/common.h"

struct ssl_st;
struct ssl_ctx_st;
struct ssl_method_st;
struct x509_st;
struct x509_name_st;
struct evp_pkey_st;
struct rsa_st;
struct bignum_st;
struct bio_st;
struct evp_md_st;
struct ssl_cipher_st;
struct crypto_threadid_st;

namespace lsxp {

typedef int          (*Fn_int_void)(void);
typedef void         (*Fn_void_void)(void);
typedef unsigned long (*Fn_ulong_void)(void);
typedef void         (*Fn_void_int)(int);
typedef const char*  (*Fn_cstr_int)(int);

struct OpenSslApi
{
    // ---- library ---------------------------------------------------------
    int          (*SSL_library_init)(void);
    void         (*SSL_load_error_strings)(void);
    void         (*OPENSSL_add_all_algorithms_conf)(void);
    void         (*ERR_load_crypto_strings)(void);
    unsigned long (*ERR_get_error)(void);
    void         (*ERR_error_string_n)(unsigned long error, char* buffer, size_t length);
    void         (*ERR_clear_error)(void);
    const char*  (*SSLeay_version)(int type);
    int          (*CRYPTO_num_locks)(void);
    void         (*CRYPTO_set_locking_callback)(void (*callback)(int, int, const char*, int));
    void         (*CRYPTO_THREADID_set_callback)(void (*callback)(crypto_threadid_st*));
    void         (*CRYPTO_THREADID_set_numeric)(crypto_threadid_st* id, unsigned long value);
    int          (*RAND_bytes)(unsigned char* buffer, int length);

    // ---- contexts and methods -------------------------------------------
    ssl_ctx_st*  (*SSL_CTX_new)(const ssl_method_st* method);
    void         (*SSL_CTX_free)(ssl_ctx_st* context);
    const ssl_method_st* (*TLSv1_2_client_method)(void);
    const ssl_method_st* (*TLSv1_2_server_method)(void);
    const ssl_method_st* (*TLSv1_client_method)(void);
    const ssl_method_st* (*TLSv1_server_method)(void);
    const ssl_method_st* (*SSLv23_client_method)(void);
    const ssl_method_st* (*SSLv23_server_method)(void);
    void         (*SSL_CTX_set_verify)(ssl_ctx_st* context, int mode, int (*callback)(int, void*));
    void         (*SSL_CTX_set_verify_depth)(ssl_ctx_st* context, int depth);
    int          (*SSL_CTX_use_certificate_file)(ssl_ctx_st* context, const char* file, int type);
    int          (*SSL_CTX_use_PrivateKey_file)(ssl_ctx_st* context, const char* file, int type);
    int          (*SSL_CTX_use_certificate)(ssl_ctx_st* context, x509_st* certificate);
    int          (*SSL_CTX_use_PrivateKey)(ssl_ctx_st* context, evp_pkey_st* key);
    int          (*SSL_CTX_check_private_key)(const ssl_ctx_st* context);
    int          (*SSL_CTX_set_cipher_list)(ssl_ctx_st* context, const char* list);
    int          (*SSL_CTX_load_verify_locations)(ssl_ctx_st* context, const char* caFile, const char* caPath);
    int          (*SSL_CTX_set_default_verify_paths)(ssl_ctx_st* context);
    long         (*SSL_CTX_ctrl)(ssl_ctx_st* context, int command, long argument, void* pointer);

    // ---- connections -----------------------------------------------------
    ssl_st*      (*SSL_new)(ssl_ctx_st* context);
    void         (*SSL_free)(ssl_st* ssl);
    int          (*SSL_set_fd)(ssl_st* ssl, int fd);
    int          (*SSL_connect)(ssl_st* ssl);
    int          (*SSL_accept)(ssl_st* ssl);
    int          (*SSL_read)(ssl_st* ssl, void* buffer, int length);
    int          (*SSL_write)(ssl_st* ssl, const void* buffer, int length);
    int          (*SSL_shutdown)(ssl_st* ssl);
    int          (*SSL_get_error)(const ssl_st* ssl, int result);
    long         (*SSL_get_verify_result)(const ssl_st* ssl);
    int          (*SSL_pending)(const ssl_st* ssl);
    void         (*SSL_set_verify)(ssl_st* ssl, int mode, int (*callback)(int, void*));
    long         (*SSL_ctrl)(ssl_st* ssl, int command, long argument, void* pointer);
    const char*  (*SSL_get_version)(const ssl_st* ssl);
    const ssl_cipher_st* (*SSL_get_current_cipher)(const ssl_st* ssl);
    const char*  (*SSL_CIPHER_get_name)(const ssl_cipher_st* cipher);
    x509_st*     (*SSL_get_peer_certificate)(const ssl_st* ssl);
    x509_st*     (*SSL_get_certificate)(const ssl_st* ssl);

    // ---- certificates and keys -------------------------------------------
    void         (*X509_free)(x509_st* certificate);
    int          (*X509_digest)(const x509_st* certificate, const evp_md_st* type,
                                unsigned char* digest, unsigned int* length);
    x509_name_st* (*X509_get_subject_name)(x509_st* certificate);
    char*        (*X509_NAME_oneline)(const x509_name_st* name, char* buffer, int size);
    const char*  (*X509_verify_cert_error_string)(long error);
    int          (*X509_check_host)(x509_st* certificate, const char* name, size_t nameLength,
                                    unsigned int flags, char** peerName);
    x509_st*     (*X509_new)(void);
    int          (*X509_set_version)(x509_st* certificate, long version);
    int          (*X509_set_serialNumber)(x509_st* certificate, void* serial);
    void*        (*X509_get_serialNumber)(x509_st* certificate);
    int          (*X509_set_issuer_name)(x509_st* certificate, x509_name_st* name);
    int          (*X509_set_subject_name)(x509_st* certificate, x509_name_st* name);
    x509_name_st* (*X509_get_issuer_name)(x509_st* certificate);
    int          (*X509_NAME_add_entry_by_txt)(x509_name_st* name, const char* field,
                                               int type, const unsigned char* bytes,
                                               int length, int location, int set);
    int          (*X509_set_pubkey)(x509_st* certificate, evp_pkey_st* key);
    int          (*X509_sign)(x509_st* certificate, evp_pkey_st* key, const evp_md_st* digest);
    int          (*X509_set_notBefore)(x509_st* certificate, const void* time);
    int          (*X509_set_notAfter)(x509_st* certificate, const void* time);
    void*        (*X509_gmtime_adj)(void* time, long offset);
    int          (*X509_get_ext_by_NID)(const x509_st* certificate, int nid, int lastPosition);

    // ---- evp / rsa / bn / bio / pem --------------------------------------
    const evp_md_st* (*EVP_sha256)(void);
    evp_pkey_st* (*EVP_PKEY_new)(void);
    void         (*EVP_PKEY_free)(evp_pkey_st* key);
    int          (*EVP_PKEY_assign)(evp_pkey_st* key, int type, void* value);
    rsa_st*      (*EVP_PKEY_get1_RSA)(evp_pkey_st* key);
    int          (*EVP_PKEY_set1_RSA)(evp_pkey_st* key, rsa_st* rsa);
    rsa_st*      (*RSA_new)(void);
    void         (*RSA_free)(rsa_st* rsa);
    int          (*RSA_generate_key_ex)(rsa_st* rsa, int bits, bignum_st* exponent, void* callback);
    bignum_st*   (*BN_new)(void);
    void         (*BN_free)(bignum_st* number);
    int          (*BN_set_word)(bignum_st* number, unsigned long value);
    int          (*BN_num_bits)(const bignum_st* number);
    bio_st*      (*BIO_new_file)(const char* file, const char* mode);
    bio_st*      (*BIO_new_mem_buf)(void* buffer, int length);
    bio_st*      (*BIO_new)(const void* method);
    const void*  (*BIO_s_mem)(void);
    int          (*BIO_free)(bio_st* bio);
    void         (*BIO_free_all)(bio_st* bio);
    long         (*BIO_ctrl)(bio_st* bio, int command, long argument, void* pointer);
    x509_st*     (*PEM_read_bio_X509)(bio_st* bio, x509_st** certificate, void* callback, void* userData);
    evp_pkey_st* (*PEM_read_bio_PrivateKey)(bio_st* bio, evp_pkey_st** key, void* callback, void* userData);
    int          (*PEM_write_bio_X509)(bio_st* bio, x509_st* certificate);
    int          (*PEM_write_bio_PrivateKey)(bio_st* bio, evp_pkey_st* key, const void* cipher,
                                             unsigned char* password, int passwordLength,
                                             void* callback, void* userData);
    int          (*PEM_write_bio_RSA_PUBKEY)(bio_st* bio, rsa_st* rsa);
    int          (*ASN1_INTEGER_set)(void* integer, long value);
    void         (*ASN1_TIME_free)(void* time);

    // ---- shutdown --------------------------------------------------------
    void         (*EVP_cleanup)(void);
    void         (*CRYPTO_cleanup_all_ex_data)(void);
    void         (*ERR_remove_state)(unsigned long threadId);

    bool IsLoaded() const { return m_loaded; }
    const std::string& LoadError() const { return m_loadError; }
    bool Load();
    void Unload();

private:
    OpenSslApi();
    friend OpenSslApi& SslApi();

    bool        m_loaded;
    HMODULE     m_crypto;
    HMODULE     m_ssl;
    std::string m_loadError;
};

// Accessor used by the crypto and TLS network sources.
OpenSslApi& SslApi();

// Numeric OpenSSL constants we need (values are part of the stable ABI).
enum
{
    SSL_FILETYPE_PEM   = 1,
    SSL_VERIFY_NONE    = 0x00,
    SSL_VERIFY_PEER    = 0x01,
    SSL_VERIFY_FAIL_IF_NO_PEER_CERT = 0x02,
    SSL_ERROR_NONE     = 0,
    SSL_ERROR_SSL      = 1,
    SSL_ERROR_WANT_READ  = 2,
    SSL_ERROR_WANT_WRITE = 3,
    SSL_ERROR_SYSCALL  = 5,
    SSL_ERROR_ZERO_RETURN = 6,
    SSL_CTRL_SET_TLSEXT_HOSTNAME = 55,
    TLSEXT_NAMETYPE_host_name    = 0,
    SSL_OP_NO_SSLv2 = 0x01000000L,
    SSL_OP_NO_SSLv3 = 0x02000000L,
    SSL_OP_NO_TLSv1 = 0x04000000L,
    SSL_OP_NO_TLSv1_1 = 0x10000000L,
    SSL_OP_NO_TLSv1_2 = 0x08000000L,
    SSL_OP_NO_COMPRESSION = 0x00020000L,
    EVP_PKEY_RSA = 6,
    MBSTRING_ASC = 0x1000 | 1,
    X509_VERSION_3 = 2
    , SSLEAY_VERSION = 0
    , SSL_CTRL_OPTIONS = 32
};

}  // namespace lsxp

#endif  // LSXP_OPENSSL_API_H
