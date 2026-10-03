#include "bounce/util/Json.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

namespace bounce::util
{

namespace
{
const Json& nullJson()
{
    static const Json n;
    return n;
}

const Json::Array& emptyArray()
{
    static const Json::Array a;
    return a;
}

const Json::Object& emptyObject()
{
    static const Json::Object o;
    return o;
}

struct ParseError : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

class Parser
{
public:
    explicit Parser (std::string_view t) : text (t) {}

    Json parseDocument()
    {
        auto v = parseValue (0);
        skipWhitespace();
        if (pos != text.size())
            fail ("unexpected trailing characters");
        return v;
    }

    int line() const
    {
        int l = 1;
        for (size_t i = 0; i < pos && i < text.size(); ++i)
            l += text[i] == '\n' ? 1 : 0;
        return l;
    }

private:
    std::string_view text;
    size_t pos = 0;

    [[noreturn]] void fail (const std::string& msg) { throw ParseError (msg); }

    void skipWhitespace()
    {
        while (pos < text.size())
        {
            const char c = text[pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                ++pos;
            else if (c == '/' && pos + 1 < text.size() && text[pos + 1] == '/')
            {
                // Allow // comments: style presets are meant to be hand-edited.
                while (pos < text.size() && text[pos] != '\n')
                    ++pos;
            }
            else
                break;
        }
    }

    bool consume (char c)
    {
        skipWhitespace();
        if (pos < text.size() && text[pos] == c)
        {
            ++pos;
            return true;
        }
        return false;
    }

    void expect (char c)
    {
        if (! consume (c))
            fail (std::string ("expected '") + c + "'");
    }

    bool consumeWord (std::string_view w)
    {
        if (text.substr (pos, w.size()) == w)
        {
            pos += w.size();
            return true;
        }
        return false;
    }

    Json parseValue (int depth)
    {
        if (depth > 64)
            fail ("nesting too deep");

        skipWhitespace();
        if (pos >= text.size())
            fail ("unexpected end of input");

        const char c = text[pos];
        if (c == '{') return parseObject (depth);
        if (c == '[') return parseArray (depth);
        if (c == '"') return Json (parseString());
        if (consumeWord ("true"))  return Json (true);
        if (consumeWord ("false")) return Json (false);
        if (consumeWord ("null"))  return Json();
        if (c == '-' || (c >= '0' && c <= '9')) return Json (parseNumber());
        fail (std::string ("unexpected character '") + c + "'");
    }

    Json parseObject (int depth)
    {
        expect ('{');
        Json::Object obj;
        if (consume ('}'))
            return Json (std::move (obj));

        for (;;)
        {
            skipWhitespace();
            if (pos >= text.size() || text[pos] != '"')
                fail ("expected string key");
            auto key = parseString();
            expect (':');
            obj.emplace_back (std::move (key), parseValue (depth + 1));
            if (consume (','))
            {
                if (consume ('}')) // tolerate a trailing comma
                    break;
                continue;
            }
            expect ('}');
            break;
        }
        return Json (std::move (obj));
    }

    Json parseArray (int depth)
    {
        expect ('[');
        Json::Array arr;
        if (consume (']'))
            return Json (std::move (arr));

        for (;;)
        {
            arr.push_back (parseValue (depth + 1));
            if (consume (','))
            {
                if (consume (']'))
                    break;
                continue;
            }
            expect (']');
            break;
        }
        return Json (std::move (arr));
    }

    static void appendUtf8 (std::string& out, uint32_t cp)
    {
        if (cp < 0x80)
            out += static_cast<char> (cp);
        else if (cp < 0x800)
        {
            out += static_cast<char> (0xC0 | (cp >> 6));
            out += static_cast<char> (0x80 | (cp & 0x3F));
        }
        else if (cp < 0x10000)
        {
            out += static_cast<char> (0xE0 | (cp >> 12));
            out += static_cast<char> (0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char> (0x80 | (cp & 0x3F));
        }
        else
        {
            out += static_cast<char> (0xF0 | (cp >> 18));
            out += static_cast<char> (0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char> (0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char> (0x80 | (cp & 0x3F));
        }
    }

    uint32_t parseHex4()
    {
        if (pos + 4 > text.size())
            fail ("bad \\u escape");
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i)
        {
            const char h = text[pos++];
            v <<= 4;
            if (h >= '0' && h <= '9')      v |= static_cast<uint32_t> (h - '0');
            else if (h >= 'a' && h <= 'f') v |= static_cast<uint32_t> (h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') v |= static_cast<uint32_t> (h - 'A' + 10);
            else fail ("bad \\u escape");
        }
        return v;
    }

    std::string parseString()
    {
        ++pos; // opening quote
        std::string out;
        while (pos < text.size())
        {
            const char c = text[pos++];
            if (c == '"')
                return out;
            if (c != '\\')
            {
                out += c;
                continue;
            }
            if (pos >= text.size())
                break;
            const char e = text[pos++];
            switch (e)
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
                    uint32_t cp = parseHex4();
                    if (cp >= 0xD800 && cp <= 0xDBFF && text.substr (pos, 2) == "\\u")
                    {
                        pos += 2;
                        const uint32_t lo = parseHex4();
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    appendUtf8 (out, cp);
                    break;
                }
                default: fail ("bad escape");
            }
        }
        fail ("unterminated string");
    }

    double parseNumber()
    {
        const size_t start = pos;
        if (text[pos] == '-')
            ++pos;
        while (pos < text.size())
        {
            const char c = text[pos];
            if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-')
                ++pos;
            else
                break;
        }
        const std::string num (text.substr (start, pos - start));
        char* end = nullptr;
        const double v = std::strtod (num.c_str(), &end);
        if (end == num.c_str() || *end != '\0')
            fail ("bad number");
        return v;
    }
};

void escapeTo (std::string& out, const std::string& s)
{
    out += '"';
    for (char c : s)
    {
        switch (c)
        {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char> (c) < 0x20)
                {
                    char buf[8];
                    std::snprintf (buf, sizeof (buf), "\\u%04x", c);
                    out += buf;
                }
                else
                    out += c;
        }
    }
    out += '"';
}
} // namespace

bool Json::asBool (bool fallback) const
{
    if (auto* b = std::get_if<bool> (&value)) return *b;
    return fallback;
}

double Json::asNumber (double fallback) const
{
    if (auto* d = std::get_if<double> (&value)) return *d;
    return fallback;
}

int Json::asInt (int fallback) const
{
    if (auto* d = std::get_if<double> (&value)) return static_cast<int> (std::lround (*d));
    return fallback;
}

std::string Json::asString (const std::string& fallback) const
{
    if (auto* s = std::get_if<std::string> (&value)) return *s;
    return fallback;
}

const Json::Array& Json::asArray() const
{
    if (auto* a = std::get_if<Array> (&value)) return *a;
    return emptyArray();
}

const Json::Object& Json::asObject() const
{
    if (auto* o = std::get_if<Object> (&value)) return *o;
    return emptyObject();
}

Json::Array& Json::asArray()
{
    if (! isArray()) value = Array {};
    return std::get<Array> (value);
}

Json::Object& Json::asObject()
{
    if (! isObject()) value = Object {};
    return std::get<Object> (value);
}

const Json& Json::operator[] (std::string_view key) const
{
    for (const auto& [k, v] : asObject())
        if (k == key)
            return v;
    return nullJson();
}

bool Json::has (std::string_view key) const
{
    for (const auto& [k, v] : asObject())
        if (k == key)
            return true;
    return false;
}

Json& Json::set (std::string key, Json v)
{
    auto& obj = asObject();
    for (auto& [k, existing] : obj)
    {
        if (k == key)
        {
            existing = std::move (v);
            return existing;
        }
    }
    obj.emplace_back (std::move (key), std::move (v));
    return obj.back().second;
}

Json& Json::push (Json v)
{
    auto& arr = asArray();
    arr.push_back (std::move (v));
    return arr.back();
}

std::string Json::dump (int indent) const
{
    std::string out;
    dumpTo (out, indent, 0);
    return out;
}

void Json::dumpTo (std::string& out, int indent, int depth) const
{
    const auto newline = [&] (int d)
    {
        if (indent <= 0)
            return;
        out += '\n';
        out.append (static_cast<size_t> (indent * d), ' ');
    };

    if (isNull())
        out += "null";
    else if (auto* b = std::get_if<bool> (&value))
        out += *b ? "true" : "false";
    else if (auto* d = std::get_if<double> (&value))
    {
        if (std::isfinite (*d) && std::floor (*d) == *d && std::abs (*d) < 1e15)
            out += std::to_string (static_cast<long long> (*d));
        else
        {
            char buf[32];
            std::snprintf (buf, sizeof (buf), "%.10g", std::isfinite (*d) ? *d : 0.0);
            out += buf;
        }
    }
    else if (auto* s = std::get_if<std::string> (&value))
        escapeTo (out, *s);
    else if (auto* a = std::get_if<Array> (&value))
    {
        out += '[';
        for (size_t i = 0; i < a->size(); ++i)
        {
            if (i > 0) out += ',';
            newline (depth + 1);
            (*a)[i].dumpTo (out, indent, depth + 1);
        }
        if (! a->empty()) newline (depth);
        out += ']';
    }
    else if (auto* o = std::get_if<Object> (&value))
    {
        out += '{';
        for (size_t i = 0; i < o->size(); ++i)
        {
            if (i > 0) out += ',';
            newline (depth + 1);
            escapeTo (out, (*o)[i].first);
            out += indent > 0 ? ": " : ":";
            (*o)[i].second.dumpTo (out, indent, depth + 1);
        }
        if (! o->empty()) newline (depth);
        out += '}';
    }
}

Json::ParseResult Json::parse (std::string_view text)
{
    Parser parser (text);
    try
    {
        return { parser.parseDocument(), {}, 0 };
    }
    catch (const ParseError& e)
    {
        return { std::nullopt, e.what(), parser.line() };
    }
}

} // namespace bounce::util
