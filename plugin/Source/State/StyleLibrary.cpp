#include "State/StyleLibrary.h"

#include "BinaryData.h"

#include <algorithm>

namespace bounce
{

namespace
{
std::optional<gen::StylePreset> parsePreset (const juce::String& text, const juce::String& source, juce::StringArray& warnings)
{
    const auto parsed = util::Json::parse (text.toStdString());
    if (! parsed.value)
    {
        warnings.add (source + ": " + juce::String (parsed.error) + " (line " + juce::String (parsed.line) + ")");
        return std::nullopt;
    }

    std::vector<std::string> w;
    auto preset = gen::StylePreset::fromJson (*parsed.value, &w);
    for (const auto& msg : w)
        warnings.add (source + ": " + juce::String (msg));
    return preset;
}
} // namespace

StyleLibrary::StyleLibrary()
{
    reload();
}

juce::File StyleLibrary::userStyleFolder()
{
   #if JUCE_MAC
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Application Support/Bounce/Styles");
   #else
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Bounce/Styles");
   #endif
}

juce::StringArray StyleLibrary::reload()
{
    juce::StringArray warnings;
    std::vector<gen::StylePreset> loaded;

    for (int i = 0; i < BounceBinary::namedResourceListSize; ++i)
    {
        int size = 0;
        const auto* data = BounceBinary::getNamedResource (BounceBinary::namedResourceList[i], size);
        if (data == nullptr)
            continue;
        const auto text = juce::String::fromUTF8 (data, size);
        if (auto p = parsePreset (text, BounceBinary::originalFilenames[i], warnings))
            loaded.push_back (std::move (*p));
    }

    // Stable order for the bundled ones: the default style first, then alphabetical.
    std::sort (loaded.begin(), loaded.end(), [] (const auto& a, const auto& b)
    {
        if ((a.id == "swag_bounce") != (b.id == "swag_bounce"))
            return a.id == "swag_bounce";
        return a.name < b.name;
    });

    const auto folder = userStyleFolder();
    if (folder.isDirectory())
    {
        auto files = folder.findChildFiles (juce::File::findFiles, false, "*.json");
        files.sort();
        for (const auto& f : files)
        {
            auto p = parsePreset (f.loadFileAsString(), f.getFileName(), warnings);
            if (! p)
                continue;
            if (p->id.empty())
                p->id = f.getFileNameWithoutExtension().toStdString();

            auto existing = std::find_if (loaded.begin(), loaded.end(), [&] (const auto& s) { return s.id == p->id; });
            if (existing != loaded.end())
                *existing = std::move (*p);
            else
                loaded.push_back (std::move (*p));
        }
    }

    if (loaded.empty())
        loaded.push_back (gen::StylePreset::defaults());

    styles = std::move (loaded);
    return warnings;
}

const gen::StylePreset& StyleLibrary::get (int index) const
{
    return styles[static_cast<size_t> (juce::jlimit (0, static_cast<int> (styles.size()) - 1, index))];
}

int StyleLibrary::indexOf (const std::string& id) const
{
    for (size_t i = 0; i < styles.size(); ++i)
        if (styles[i].id == id)
            return static_cast<int> (i);
    return 0;
}

} // namespace bounce
