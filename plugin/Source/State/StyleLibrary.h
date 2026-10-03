#pragma once

#include "bounce/gen/StylePreset.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace bounce
{

/** Bundled style presets (compiled in from presets/styles) plus the user's own JSON files.
    A user file with the same id as a bundled preset replaces it. Message thread only. */
class StyleLibrary
{
public:
    StyleLibrary();

    /** Reloads bundled + user presets. Returns warnings to show the user (bad JSON etc). */
    juce::StringArray reload();

    const std::vector<gen::StylePreset>& presets() const { return styles; }
    const gen::StylePreset& get (int index) const;

    /** Index of a preset by id, or 0 (the first preset) if it isn't found. */
    int indexOf (const std::string& id) const;

    /** %APPDATA%\Bounce\Styles, ~/Library/Application Support/Bounce/Styles, ~/.config/Bounce/Styles. */
    static juce::File userStyleFolder();

private:
    std::vector<gen::StylePreset> styles;
};

} // namespace bounce
