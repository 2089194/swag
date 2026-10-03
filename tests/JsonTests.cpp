#include <doctest/doctest.h>

#include "bounce/util/Json.h"

using bounce::util::Json;

TEST_CASE ("json parse basics")
{
    const auto r = Json::parse (R"({
        // comments are allowed in presets
        "name": "Swag \"Bounce\"",
        "n": -1.5e2,
        "list": [1, 2, 3,],
        "nested": { "ok": true, "none": null },
        "unicode": "é😀"
    })");
    REQUIRE_MESSAGE (r.value, r.error);
    const auto& j = *r.value;
    CHECK (j["name"].asString() == "Swag \"Bounce\"");
    CHECK (j["n"].asNumber() == -150.0);
    CHECK (j["list"].asArray().size() == 3);
    CHECK (j["nested"]["ok"].asBool());
    CHECK (j["nested"]["none"].isNull());
    CHECK (j["unicode"].asString() == "\xC3\xA9\xF0\x9F\x98\x80");
    CHECK (j["missing"]["deeper"].isNull());
    CHECK (j["missing"].asInt (7) == 7);
}

TEST_CASE ("json errors report a line")
{
    const auto r = Json::parse ("{\n  \"a\": 1,\n  \"b\": tru\n}");
    CHECK_FALSE (r.value);
    CHECK_FALSE (r.error.empty());
    CHECK (r.line == 3);

    CHECK_FALSE (Json::parse ("").value);
    CHECK_FALSE (Json::parse ("[1, 2").value);
    CHECK_FALSE (Json::parse ("{\"a\" 1}").value);
    CHECK_FALSE (Json::parse ("\"unterminated").value);
    CHECK_FALSE (Json::parse ("1 2").value);
}

TEST_CASE ("json dump round trip")
{
    Json j;
    j.set ("a", 1);
    j.set ("b", "two\nlines");
    j.set ("c", 0.25);
    Json arr;
    arr.push (true);
    arr.push (Json());
    j.set ("d", arr);
    j.set ("a", 5); // replace keeps position

    for (int indent : { 0, 2 })
    {
        const auto text = j.dump (indent);
        const auto back = Json::parse (text);
        REQUIRE (back.value);
        CHECK (back.value->dump (0) == j.dump (0));
    }
    CHECK (j.dump (0) == R"({"a":5,"b":"two\nlines","c":0.25,"d":[true,null]})");
}
