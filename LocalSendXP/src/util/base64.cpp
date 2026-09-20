#include "lsxp/base64.h"

namespace lsxp {

namespace {

const char* const kAlphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int DecodeChar(char ch)
{
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
    if (ch >= '0' && ch <= '9') return ch - '0' + 52;
    if (ch == '+') return 62;
    if (ch == '/') return 63;
    return -1;
}

}  // namespace

std::string Base64Encode(const unsigned char* data, size_t length)
{
    std::string result;
    if (data == NULL || length == 0)
    {
        return result;
    }
    result.reserve(((length + 2) / 3) * 4);

    size_t i = 0;
    while (i + 2 < length)
    {
        unsigned int value = ((unsigned int)data[i] << 16) |
                             ((unsigned int)data[i + 1] << 8) |
                             ((unsigned int)data[i + 2]);
        result += kAlphabet[(value >> 18) & 0x3F];
        result += kAlphabet[(value >> 12) & 0x3F];
        result += kAlphabet[(value >> 6) & 0x3F];
        result += kAlphabet[value & 0x3F];
        i += 3;
    }

    size_t remaining = length - i;
    if (remaining == 1)
    {
        unsigned int value = (unsigned int)data[i] << 16;
        result += kAlphabet[(value >> 18) & 0x3F];
        result += kAlphabet[(value >> 12) & 0x3F];
        result += '=';
        result += '=';
    }
    else if (remaining == 2)
    {
        unsigned int value = ((unsigned int)data[i] << 16) | ((unsigned int)data[i + 1] << 8);
        result += kAlphabet[(value >> 18) & 0x3F];
        result += kAlphabet[(value >> 12) & 0x3F];
        result += kAlphabet[(value >> 6) & 0x3F];
        result += '=';
    }
    return result;
}

std::string Base64Encode(const std::string& data)
{
    return Base64Encode((const unsigned char*)data.data(), data.size());
}

bool Base64Decode(const std::string& text, std::string& out)
{
    out.clear();
    unsigned int accumulator = 0;
    int bits = 0;

    for (size_t i = 0; i < text.size(); ++i)
    {
        char ch = text[i];
        if (ch == '=')
        {
            break;
        }
        if (ch == '\r' || ch == '\n' || ch == ' ' || ch == '\t')
        {
            continue;
        }
        int value = DecodeChar(ch);
        if (value < 0)
        {
            return false;
        }
        accumulator = (accumulator << 6) | (unsigned int)value;
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out += (char)((accumulator >> bits) & 0xFF);
        }
    }
    return true;
}

}  // namespace lsxp
