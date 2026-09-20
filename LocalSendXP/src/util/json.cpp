#include "lsxp/json.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

namespace lsxp {

namespace {

JsonValue g_nullValue;

void AppendUtf8(std::string& out, unsigned int code)
{
    if (code < 0x80)
    {
        out += (char)code;
    }
    else if (code < 0x800)
    {
        out += (char)(0xC0 | (code >> 6));
        out += (char)(0x80 | (code & 0x3F));
    }
    else if (code < 0x10000)
    {
        out += (char)(0xE0 | (code >> 12));
        out += (char)(0x80 | ((code >> 6) & 0x3F));
        out += (char)(0x80 | (code & 0x3F));
    }
    else
    {
        out += (char)(0xF0 | (code >> 18));
        out += (char)(0x80 | ((code >> 12) & 0x3F));
        out += (char)(0x80 | ((code >> 6) & 0x3F));
        out += (char)(0x80 | (code & 0x3F));
    }
}

int HexDigit(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

class JsonParser
{
public:
    JsonParser(const std::string& text) : m_text(text), m_position(0), m_depth(0) {}

    bool Parse(JsonValue& out, std::string& errorText)
    {
        SkipWhitespace();
        if (!ParseValue(out, errorText))
        {
            return false;
        }
        SkipWhitespace();
        if (m_position != m_text.size())
        {
            errorText = "trailing characters after JSON value";
            return false;
        }
        return true;
    }

private:
    void SkipWhitespace()
    {
        while (m_position < m_text.size())
        {
            char ch = m_text[m_position];
            if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n')
            {
                ++m_position;
            }
            else
            {
                break;
            }
        }
    }

    bool Fail(std::string& errorText, const char* message)
    {
        char buffer[256];
        _snprintf(buffer, sizeof(buffer) - 1, "%s at offset %u", message, (unsigned int)m_position);
        buffer[sizeof(buffer) - 1] = '\0';
        errorText = buffer;
        return false;
    }

    bool ParseValue(JsonValue& out, std::string& errorText)
    {
        if (m_depth > 64)
        {
            return Fail(errorText, "JSON nesting too deep");
        }
        if (m_position >= m_text.size())
        {
            return Fail(errorText, "unexpected end of JSON text");
        }

        char ch = m_text[m_position];
        switch (ch)
        {
        case '{': return ParseObject(out, errorText);
        case '[': return ParseArray(out, errorText);
        case '"':
            {
                std::string text;
                if (!ParseString(text, errorText))
                {
                    return false;
                }
                out = JsonValue(text);
                return true;
            }
        case 't':
            if (m_text.compare(m_position, 4, "true") == 0)
            {
                m_position += 4;
                out = JsonValue(true);
                return true;
            }
            return Fail(errorText, "invalid literal");
        case 'f':
            if (m_text.compare(m_position, 5, "false") == 0)
            {
                m_position += 5;
                out = JsonValue(false);
                return true;
            }
            return Fail(errorText, "invalid literal");
        case 'n':
            if (m_text.compare(m_position, 4, "null") == 0)
            {
                m_position += 4;
                out = JsonValue();
                return true;
            }
            return Fail(errorText, "invalid literal");
        default:
            return ParseNumber(out, errorText);
        }
    }

    bool ParseObject(JsonValue& out, std::string& errorText)
    {
        ++m_position;  // '{'
        out = JsonValue::MakeObject();
        ++m_depth;

        SkipWhitespace();
        if (m_position < m_text.size() && m_text[m_position] == '}')
        {
            ++m_position;
            --m_depth;
            return true;
        }

        for (;;)
        {
            SkipWhitespace();
            if (m_position >= m_text.size() || m_text[m_position] != '"')
            {
                --m_depth;
                return Fail(errorText, "expected object key");
            }

            std::string key;
            if (!ParseString(key, errorText))
            {
                --m_depth;
                return false;
            }

            SkipWhitespace();
            if (m_position >= m_text.size() || m_text[m_position] != ':')
            {
                --m_depth;
                return Fail(errorText, "expected ':'");
            }
            ++m_position;

            SkipWhitespace();
            JsonValue value;
            if (!ParseValue(value, errorText))
            {
                --m_depth;
                return false;
            }
            out.Set(key, value);

            SkipWhitespace();
            if (m_position >= m_text.size())
            {
                --m_depth;
                return Fail(errorText, "unterminated object");
            }
            if (m_text[m_position] == ',')
            {
                ++m_position;
                continue;
            }
            if (m_text[m_position] == '}')
            {
                ++m_position;
                --m_depth;
                return true;
            }
            --m_depth;
            return Fail(errorText, "expected ',' or '}'");
        }
    }

    bool ParseArray(JsonValue& out, std::string& errorText)
    {
        ++m_position;  // '['
        out = JsonValue::MakeArray();
        ++m_depth;

        SkipWhitespace();
        if (m_position < m_text.size() && m_text[m_position] == ']')
        {
            ++m_position;
            --m_depth;
            return true;
        }

        for (;;)
        {
            SkipWhitespace();
            JsonValue value;
            if (!ParseValue(value, errorText))
            {
                --m_depth;
                return false;
            }
            out.Append(value);

            SkipWhitespace();
            if (m_position >= m_text.size())
            {
                --m_depth;
                return Fail(errorText, "unterminated array");
            }
            if (m_text[m_position] == ',')
            {
                ++m_position;
                continue;
            }
            if (m_text[m_position] == ']')
            {
                ++m_position;
                --m_depth;
                return true;
            }
            --m_depth;
            return Fail(errorText, "expected ',' or ']'");
        }
    }

    bool ParseString(std::string& out, std::string& errorText)
    {
        ++m_position;  // opening quote
        out.clear();

        while (m_position < m_text.size())
        {
            char ch = m_text[m_position++];
            if (ch == '"')
            {
                return true;
            }
            if (ch != '\\')
            {
                out += ch;
                continue;
            }

            if (m_position >= m_text.size())
            {
                break;
            }
            char escape = m_text[m_position++];
            switch (escape)
            {
            case '"':  out += '"';  break;
            case '\\': out += '\\'; break;
            case '/':  out += '/';  break;
            case 'b':  out += '\b'; break;
            case 'f':  out += '\f'; break;
            case 'n':  out += '\n'; break;
            case 'r':  out += '\r'; break;
            case 't':  out += '\t'; break;
            case 'u':
                {
                    if (m_position + 4 > m_text.size())
                    {
                        return Fail(errorText, "truncated \\u escape");
                    }
                    unsigned int code = 0;
                    for (int i = 0; i < 4; ++i)
                    {
                        int digit = HexDigit(m_text[m_position + i]);
                        if (digit < 0)
                        {
                            return Fail(errorText, "invalid \\u escape");
                        }
                        code = (code << 4) | (unsigned int)digit;
                    }
                    m_position += 4;

                    if (code >= 0xD800 && code <= 0xDBFF &&
                        m_position + 6 <= m_text.size() &&
                        m_text[m_position] == '\\' && m_text[m_position + 1] == 'u')
                    {
                        unsigned int low = 0;
                        bool ok = true;
                        for (int i = 0; i < 4; ++i)
                        {
                            int digit = HexDigit(m_text[m_position + 2 + i]);
                            if (digit < 0)
                            {
                                ok = false;
                                break;
                            }
                            low = (low << 4) | (unsigned int)digit;
                        }
                        if (ok && low >= 0xDC00 && low <= 0xDFFF)
                        {
                            m_position += 6;
                            code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                        }
                    }
                    AppendUtf8(out, code);
                }
                break;
            default:
                return Fail(errorText, "invalid escape sequence");
            }
        }
        return Fail(errorText, "unterminated string");
    }

    bool ParseNumber(JsonValue& out, std::string& errorText)
    {
        size_t begin = m_position;
        if (m_position < m_text.size() && (m_text[m_position] == '-' || m_text[m_position] == '+'))
        {
            ++m_position;
        }
        bool digits = false;
        while (m_position < m_text.size() && m_text[m_position] >= '0' && m_text[m_position] <= '9')
        {
            ++m_position;
            digits = true;
        }
        if (m_position < m_text.size() && m_text[m_position] == '.')
        {
            ++m_position;
            while (m_position < m_text.size() && m_text[m_position] >= '0' && m_text[m_position] <= '9')
            {
                ++m_position;
                digits = true;
            }
        }
        if (!digits)
        {
            return Fail(errorText, "invalid value");
        }
        if (m_position < m_text.size() &&
            (m_text[m_position] == 'e' || m_text[m_position] == 'E'))
        {
            ++m_position;
            if (m_position < m_text.size() && (m_text[m_position] == '-' || m_text[m_position] == '+'))
            {
                ++m_position;
            }
            while (m_position < m_text.size() && m_text[m_position] >= '0' && m_text[m_position] <= '9')
            {
                ++m_position;
            }
        }

        std::string text = m_text.substr(begin, m_position - begin);
        out = JsonValue(atof(text.c_str()));
        return true;
    }

    const std::string& m_text;
    size_t m_position;
    int    m_depth;
};

}  // namespace

JsonValue::JsonValue()
    : m_type(TYPE_NULL), m_bool(false), m_number(0.0)
{
}

JsonValue::JsonValue(bool value)
    : m_type(TYPE_BOOL), m_bool(value), m_number(0.0)
{
}

JsonValue::JsonValue(int value)
    : m_type(TYPE_NUMBER), m_bool(false), m_number((double)value)
{
}

JsonValue::JsonValue(unsigned int value)
    : m_type(TYPE_NUMBER), m_bool(false), m_number((double)value)
{
}

JsonValue::JsonValue(double value)
    : m_type(TYPE_NUMBER), m_bool(false), m_number(value)
{
}

JsonValue::JsonValue(int64 value)
    : m_type(TYPE_NUMBER), m_bool(false), m_number((double)value)
{
}

JsonValue::JsonValue(const char* value)
    : m_type(TYPE_STRING), m_bool(false), m_number(0.0), m_string(value != NULL ? value : "")
{
}

JsonValue::JsonValue(const std::string& value)
    : m_type(TYPE_STRING), m_bool(false), m_number(0.0), m_string(value)
{
}

JsonValue JsonValue::MakeObject()
{
    JsonValue value;
    value.m_type = TYPE_OBJECT;
    return value;
}

JsonValue JsonValue::MakeArray()
{
    JsonValue value;
    value.m_type = TYPE_ARRAY;
    return value;
}

void JsonValue::SetType(Type type)
{
    m_type = type;
    if (type != TYPE_STRING)
    {
        m_string.clear();
    }
    if (type != TYPE_ARRAY)
    {
        m_array.clear();
    }
    if (type != TYPE_OBJECT)
    {
        m_object.clear();
    }
}

bool JsonValue::AsBool(bool defaultValue) const
{
    if (m_type == TYPE_BOOL)
    {
        return m_bool;
    }
    if (m_type == TYPE_NUMBER)
    {
        return m_number != 0.0;
    }
    return defaultValue;
}

double JsonValue::AsDouble(double defaultValue) const
{
    if (m_type == TYPE_NUMBER)
    {
        return m_number;
    }
    if (m_type == TYPE_STRING)
    {
        return atof(m_string.c_str());
    }
    return defaultValue;
}

int64 JsonValue::AsInt64(int64 defaultValue) const
{
    if (m_type == TYPE_NUMBER)
    {
        return (int64)m_number;
    }
    if (m_type == TYPE_STRING)
    {
        return (int64)_atoi64(m_string.c_str());
    }
    if (m_type == TYPE_BOOL)
    {
        return m_bool ? 1 : 0;
    }
    return defaultValue;
}

std::string JsonValue::AsString() const
{
    if (m_type == TYPE_STRING)
    {
        return m_string;
    }
    return std::string();
}

std::string JsonValue::AsStringOr(const std::string& defaultValue) const
{
    if (m_type == TYPE_STRING)
    {
        return m_string;
    }
    return defaultValue;
}

void JsonValue::Set(const std::string& key, const JsonValue& value)
{
    if (m_type != TYPE_OBJECT)
    {
        SetType(TYPE_OBJECT);
    }
    m_object[key] = value;
}

bool JsonValue::Has(const std::string& key) const
{
    if (m_type != TYPE_OBJECT)
    {
        return false;
    }
    return m_object.find(key) != m_object.end();
}

const JsonValue& JsonValue::Get(const std::string& key) const
{
    if (m_type == TYPE_OBJECT)
    {
        std::map<std::string, JsonValue>::const_iterator it = m_object.find(key);
        if (it != m_object.end())
        {
            return it->second;
        }
    }
    return g_nullValue;
}

void JsonValue::Append(const JsonValue& value)
{
    if (m_type != TYPE_ARRAY)
    {
        SetType(TYPE_ARRAY);
    }
    m_array.push_back(value);
}

size_t JsonValue::Size() const
{
    if (m_type == TYPE_ARRAY)
    {
        return m_array.size();
    }
    if (m_type == TYPE_OBJECT)
    {
        return m_object.size();
    }
    return 0;
}

const JsonValue& JsonValue::At(size_t index) const
{
    if (m_type == TYPE_ARRAY && index < m_array.size())
    {
        return m_array[index];
    }
    return g_nullValue;
}

void JsonValue::EscapeString(const std::string& text, std::string& out)
{
    out += '"';
    for (size_t i = 0; i < text.size(); ++i)
    {
        unsigned char ch = (unsigned char)text[i];
        switch (ch)
        {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b";  break;
        case '\f': out += "\\f";  break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if (ch < 0x20)
            {
                char buffer[8];
                _snprintf(buffer, sizeof(buffer) - 1, "\\u%04x", (int)ch);
                buffer[sizeof(buffer) - 1] = '\0';
                out += buffer;
            }
            else
            {
                out += (char)ch;
            }
            break;
        }
    }
    out += '"';
}

void JsonValue::SerializeTo(std::string& out) const
{
    switch (m_type)
    {
    case TYPE_NULL:
        out += "null";
        break;
    case TYPE_BOOL:
        out += m_bool ? "true" : "false";
        break;
    case TYPE_NUMBER:
        {
            double value = m_number;
            if (value == floor(value) && value > -1.0e15 && value < 1.0e15)
            {
                char buffer[48];
                _snprintf(buffer, sizeof(buffer) - 1, "%I64d", (int64)value);
                buffer[sizeof(buffer) - 1] = '\0';
                out += buffer;
            }
            else
            {
                char buffer[48];
                _snprintf(buffer, sizeof(buffer) - 1, "%.10g", value);
                buffer[sizeof(buffer) - 1] = '\0';
                out += buffer;
            }
        }
        break;
    case TYPE_STRING:
        EscapeString(m_string, out);
        break;
    case TYPE_ARRAY:
        {
            out += '[';
            for (size_t i = 0; i < m_array.size(); ++i)
            {
                if (i > 0)
                {
                    out += ',';
                }
                m_array[i].SerializeTo(out);
            }
            out += ']';
        }
        break;
    case TYPE_OBJECT:
        {
            out += '{';
            bool first = true;
            for (std::map<std::string, JsonValue>::const_iterator it = m_object.begin();
                 it != m_object.end(); ++it)
            {
                if (!first)
                {
                    out += ',';
                }
                first = false;
                EscapeString(it->first, out);
                out += ':';
                it->second.SerializeTo(out);
            }
            out += '}';
        }
        break;
    }
}

std::string JsonValue::Serialize() const
{
    std::string out;
    SerializeTo(out);
    return out;
}

bool JsonValue::Parse(const std::string& text, JsonValue& out, std::string& errorText)
{
    JsonParser parser(text);
    return parser.Parse(out, errorText);
}

}  // namespace lsxp
