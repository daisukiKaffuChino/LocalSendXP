#include "lsxp/sha256.h"

#include <string.h>
#include <stdio.h>

namespace lsxp {

namespace {

const unsigned int K[64] =
{
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

inline unsigned int RotateRight(unsigned int value, int bits)
{
    return (value >> bits) | (value << (32 - bits));
}

inline unsigned int Ch(unsigned int x, unsigned int y, unsigned int z)
{
    return (x & y) ^ (~x & z);
}

inline unsigned int Maj(unsigned int x, unsigned int y, unsigned int z)
{
    return (x & y) ^ (x & z) ^ (y & z);
}

inline unsigned int BigSigma0(unsigned int x)
{
    return RotateRight(x, 2) ^ RotateRight(x, 13) ^ RotateRight(x, 22);
}

inline unsigned int BigSigma1(unsigned int x)
{
    return RotateRight(x, 6) ^ RotateRight(x, 11) ^ RotateRight(x, 25);
}

inline unsigned int SmallSigma0(unsigned int x)
{
    return RotateRight(x, 7) ^ RotateRight(x, 18) ^ (x >> 3);
}

inline unsigned int SmallSigma1(unsigned int x)
{
    return RotateRight(x, 17) ^ RotateRight(x, 19) ^ (x >> 10);
}

}  // namespace

Sha256::Sha256()
{
    Reset();
}

void Sha256::Reset()
{
    m_state[0] = 0x6a09e667;
    m_state[1] = 0xbb67ae85;
    m_state[2] = 0x3c6ef372;
    m_state[3] = 0xa54ff53a;
    m_state[4] = 0x510e527f;
    m_state[5] = 0x9b05688c;
    m_state[6] = 0x1f83d9ab;
    m_state[7] = 0x5be0cd19;
    m_bitCount = 0;
    m_bufferLength = 0;
    memset(m_buffer, 0, sizeof(m_buffer));
}

void Sha256::Transform(const unsigned char block[64])
{
    unsigned int w[64];
    for (int i = 0; i < 16; ++i)
    {
        w[i] = ((unsigned int)block[i * 4] << 24) |
               ((unsigned int)block[i * 4 + 1] << 16) |
               ((unsigned int)block[i * 4 + 2] << 8) |
               ((unsigned int)block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i)
    {
        w[i] = SmallSigma1(w[i - 2]) + w[i - 7] + SmallSigma0(w[i - 15]) + w[i - 16];
    }

    unsigned int a = m_state[0];
    unsigned int b = m_state[1];
    unsigned int c = m_state[2];
    unsigned int d = m_state[3];
    unsigned int e = m_state[4];
    unsigned int f = m_state[5];
    unsigned int g = m_state[6];
    unsigned int h = m_state[7];

    for (int i = 0; i < 64; ++i)
    {
        unsigned int t1 = h + BigSigma1(e) + Ch(e, f, g) + K[i] + w[i];
        unsigned int t2 = BigSigma0(a) + Maj(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    m_state[0] += a;
    m_state[1] += b;
    m_state[2] += c;
    m_state[3] += d;
    m_state[4] += e;
    m_state[5] += f;
    m_state[6] += g;
    m_state[7] += h;
}

void Sha256::Update(const void* data, size_t length)
{
    const unsigned char* bytes = (const unsigned char*)data;
    size_t index = (size_t)((m_bitCount >> 3) & 0x3F);
    m_bitCount += (uint64)length << 3;

    size_t i = 0;
    if (index > 0)
    {
        size_t fill = 64 - index;
        if (length >= fill)
        {
            memcpy(m_buffer + index, bytes, fill);
            Transform(m_buffer);
            i = fill;
        }
        else
        {
            memcpy(m_buffer + index, bytes, length);
            m_bufferLength = index + length;
            return;
        }
    }

    for (; i + 64 <= length; i += 64)
    {
        Transform(bytes + i);
    }

    if (i < length)
    {
        memcpy(m_buffer, bytes + i, length - i);
        m_bufferLength = length - i;
    }
    else
    {
        m_bufferLength = 0;
    }
}

void Sha256::Final(unsigned char digest[32])
{
    static const unsigned char padding[64] =
    {
        0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };

    unsigned char lengthBytes[8];
    uint64 bits = m_bitCount;
    for (int i = 0; i < 8; ++i)
    {
        lengthBytes[7 - i] = (unsigned char)((bits >> (i * 8)) & 0xFF);
    }

    size_t index = (size_t)((m_bitCount >> 3) & 0x3F);
    size_t padLength = (index < 56) ? (56 - index) : (120 - index);
    Update(padding, padLength);
    Update(lengthBytes, 8);

    for (int i = 0; i < 8; ++i)
    {
        digest[i * 4]     = (unsigned char)((m_state[i] >> 24) & 0xFF);
        digest[i * 4 + 1] = (unsigned char)((m_state[i] >> 16) & 0xFF);
        digest[i * 4 + 2] = (unsigned char)((m_state[i] >> 8) & 0xFF);
        digest[i * 4 + 3] = (unsigned char)(m_state[i] & 0xFF);
    }
}

std::string Sha256::Hex(const unsigned char digest[32])
{
    static const char* digits = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (int i = 0; i < 32; ++i)
    {
        result += digits[(digest[i] >> 4) & 0x0F];
        result += digits[digest[i] & 0x0F];
    }
    return result;
}

std::string Sha256Hex(const std::string& data)
{
    Sha256 hash;
    hash.Update(data.data(), data.size());
    unsigned char digest[32];
    hash.Final(digest);
    return Sha256::Hex(digest);
}

bool Sha256File(const std::wstring& path, std::string& hexOut, std::string& errorText)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        errorText = Format("cannot open file for hashing (%lu)", GetLastError());
        return false;
    }

    Sha256 hash;
    std::vector<unsigned char> buffer(64 * 1024);
    bool ok = true;

    for (;;)
    {
        DWORD read = 0;
        if (!ReadFile(file, &buffer[0], (DWORD)buffer.size(), &read, NULL))
        {
            errorText = Format("read failed while hashing (%lu)", GetLastError());
            ok = false;
            break;
        }
        if (read == 0)
        {
            break;
        }
        hash.Update(&buffer[0], read);
    }

    CloseHandle(file);

    if (!ok)
    {
        return false;
    }

    unsigned char digest[32];
    hash.Final(digest);
    hexOut = Sha256::Hex(digest);
    return true;
}

}  // namespace lsxp
