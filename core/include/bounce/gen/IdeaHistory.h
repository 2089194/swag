#pragma once

#include "bounce/gen/Idea.h"
#include "bounce/util/Json.h"

#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace bounce::gen
{

/** Undo/redo over idea edits, plus a capped list of generated ideas the user can recall.

    - push() records an edit (lock, invert, reharmonise, transpose...). It clears the redo branch.
    - addIdea() additionally stores the idea in the idea list (only "Generate"/dice do this).
    Not thread-safe: owned and used by the message thread only. */
class IdeaHistory
{
public:
    explicit IdeaHistory (size_t maxIdeas = 50, size_t maxUndo = 200);

    void reset (const Idea& initial);

    void push (const Idea& p);
    void addIdea (const Idea& p, const std::string& label);

    bool canUndo() const { return cursor > 0; }
    bool canRedo() const { return cursor + 1 < undoStack.size(); }

    std::optional<Idea> undo();
    std::optional<Idea> redo();

    const Idea* current() const;

    struct Entry
    {
        std::string label;
        gen::Idea idea;
    };

    const std::deque<Entry>& ideas() const { return ideaList; }

    util::Json ideasToJson() const;
    void ideasFromJson (const util::Json& json);

private:
    size_t maxIdeas, maxUndo;
    std::vector<Idea> undoStack;
    size_t cursor = 0;
    std::deque<Entry> ideaList; // newest first
};

} // namespace bounce::gen
