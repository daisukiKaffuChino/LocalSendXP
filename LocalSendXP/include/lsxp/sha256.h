#ifndef LSXP_SHA256_H
#define LSXP_SHA256_H

#include "common.h"

namespace lsxp {

// Self contained SHA-256 implementation: works on Windows XP SP3 without
// depending on the CryptoAPI provider feature level.
class Sha256
{
public:
    Sha256();

    void Reset();
    void Update(const void* data, size_t length);
    void Final(unsigned char digest[32]);

    static std::string Hex(const unsigned char digest[32]);

private:
    void Transform(const unsigned char block[64]);

    unsigned int  m_state[8];
    uint64        m_bitCount;
    unsigned char m_buffer[64];
    size_t        m_bufferLength;
};

std::string Sha256Hex(const std::string& data);
bool Sha256File(const std::wstring& path, std::string& hexOut, std::string& errorText);

}  // namespace lsxp

#endif  // LSXP_SHA256_H
