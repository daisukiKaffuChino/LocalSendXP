#ifndef LSXP_BASE64_H
#define LSXP_BASE64_H

#include "common.h"

namespace lsxp {

// RFC 4648 alphabets, no line breaks.
std::string Base64Encode(const unsigned char* data, size_t length);
std::string Base64Encode(const std::string& data);
bool        Base64Decode(const std::string& text, std::string& out);

}  // namespace lsxp

#endif  // LSXP_BASE64_H
