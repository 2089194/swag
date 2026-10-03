#pragma once

#include "bounce/gen/Progression.h"
#include "bounce/util/Json.h"

#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace bounce::gen
{

/** Undo/redo over progression edits, plus a capped list of generated ideas the user can recall.

    - push() records an edit (lock, invert, reharmonise, transpose...). It clears the redo branch.
    - addIdea() additionally stores the progression in the idea list (only "Generate"/dice do this).
    Not thread-safe: owned and used by the message thread only. */
class IdeaHistory
{
public:
    explicit IdeaHistory (size_t maxIdeas = 50, size_t maxUndo = 200);

    void reset (const Progression& initial);

    void push (const Progression& p);
    void addIdea (const Progression& p, const std::string& label);

    bool canUndo() const { return cursor > 0; }
    bool canRedo() const { return cursor + 1 < undoStack.size(); }

    std::optional<Progression> undo();
    std::optional<Progression> redo();

    const Progression* current() const;

    struct Idea
    {
        std::string label;
        Progression progression;
    };

    const std::deque<Idea>& ideas() const { return ideaList; }

    util::Json ideasToJson() const;
    void ideasFromJson (const util::Json& json);

private:
    size_t maxIdeas, maxUndo;
    std::vector<Progression> undoStack;
    size_t cursor = 0;
    std::deque<Idea> ideaList; // newest first
};

} // namespace bounce::gen
