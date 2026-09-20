#ifndef LSXP_JSON_H
#define LSXP_JSON_H

#include "common.h"

namespace lsxp {

// Minimal JSON value used by the LocalSend protocol.
// Supported types: object, array, string, number, bool, null.
class JsonValue
{
public:
    enum Type
    {
        TYPE_NULL = 0,
        TYPE_BOOL,
        TYPE_NUMBER,
        TYPE_STRING,
        TYPE_ARRAY,
        TYPE_OBJECT
    };

    JsonValue();
    JsonValue(bool value);
    JsonValue(int value);
    JsonValue(unsigned int value);
    JsonValue(double value);
    JsonValue(int64 value);
    JsonValue(const char* value);
    JsonValue(const std::string& value);

    static JsonValue MakeObject();
    static JsonValue MakeArray();

    void SetType(Type type);

    Type GetType() const { return m_type; }
    bool IsNull() const   { return m_type == TYPE_NULL; }
    bool IsObject() const { return m_type == TYPE_OBJECT; }
    bool IsArray() const  { return m_type == TYPE_ARRAY; }
    bool IsString() const { return m_type == TYPE_STRING; }
    bool IsNumber() const { return m_type == TYPE_NUMBER; }
    bool IsBool() const   { return m_type == TYPE_BOOL; }

    bool        AsBool(bool defaultValue) const;
    double      AsDouble(double defaultValue) const;
    int64       AsInt64(int64 defaultValue) const;
    std::string AsString() const;
    std::string AsStringOr(const std::string& defaultValue) const;

    void Set(const std::string& key, const JsonValue& value);
    bool Has(const std::string& key) const;
    const JsonValue& Get(const std::string& key) const;
    const std::map<std::string, JsonValue>& Members() const { return m_object; }

    void Append(const JsonValue& value);
    size_t Size() const;
    const JsonValue& At(size_t index) const;

    std::string Serialize() const;
    static bool Parse(const std::string& text, JsonValue& out, std::string& errorText);

private:
    void SerializeTo(std::string& out) const;
    static void EscapeString(const std::string& text, std::string& out);

    Type       m_type;
    bool       m_bool;
    double     m_number;
    std::string m_string;
    std::vector<JsonValue> m_array;
    std::map<std::string, JsonValue> m_object;
};

}  // namespace lsxp

#endif  // LSXP_JSON_H
