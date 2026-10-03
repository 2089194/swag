#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace bounce::util
{

/** Small JSON value + parser/writer, enough for style presets and idea files.
    Kept in-core so presets can be loaded (and tested) without JUCE. */
class Json
{
public:
    using Array = std::vector<Json>;
    using Object = std::vector<std::pair<std::string, Json>>; // keeps insertion order

    Json() = default;
    Json (std::nullptr_t) {}
    Json (bool b) : value (b) {}
    Json (int n) : value (static_cast<double> (n)) {}
    Json (double n) : value (n) {}
    Json (const char* s) : value (std::string (s)) {}
    Json (std::string s) : value (std::move (s)) {}
    Json (Array a) : value (std::move (a)) {}
    Json (Object o) : value (std::move (o)) {}

    bool isNull() const   { return std::holds_alternative<std::monostate> (value); }
    bool isBool() const   { return std::holds_alternative<bool> (value); }
    bool isNumber() const { return std::holds_alternative<double> (value); }
    bool isString() const { return std::holds_alternative<std::string> (value); }
    bool isArray() const  { return std::holds_alternative<Array> (value); }
    bool isObject() const { return std::holds_alternative<Object> (value); }

    bool asBool (bool fallback = false) const;
    double asNumber (double fallback = 0.0) const;
    int asInt (int fallback = 0) const;
    std::string asString (const std::string& fallback = {}) const;

    const Array& asArray() const;
    const Object& asObject() const;
    Array& asArray();
    Object& asObject();

    /** Object member lookup; returns a shared null value when missing or not an object. */
    const Json& operator[] (std::string_view key) const;
    bool has (std::string_view key) const;

    /** Sets/replaces an object member (turns a null value into an object). */
    Json& set (std::string key, Json v);

    /** Appends to an array (turns a null value into an array). */
    Json& push (Json v);

    std::string dump (int indent = 2) const;

    struct ParseResult;
    static ParseResult parse (std::string_view text);

private:
    std::variant<std::monostate, bool, double, std::string, Array, Object> value;

    void dumpTo (std::string& out, int indent, int depth) const;
};

struct Json::ParseResult
{
    std::optional<Json> value;
    std::string error; // empty on success
    int line = 0;
};

} // namespace bounce::util
