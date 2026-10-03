#include "bounce/gen/IdeaHistory.h"

namespace bounce::gen
{

IdeaHistory::IdeaHistory (size_t maxIdeas_, size_t maxUndo_)
    : maxIdeas (maxIdeas_ > 0 ? maxIdeas_ : 1), maxUndo (maxUndo_ > 1 ? maxUndo_ : 2)
{
}

void IdeaHistory::reset (const Progression& initial)
{
    undoStack.assign (1, initial);
    cursor = 0;
}

void IdeaHistory::push (const Progression& p)
{
    if (! undoStack.empty() && undoStack[cursor] == p)
        return;

    if (! undoStack.empty())
        undoStack.resize (cursor + 1);
    undoStack.push_back (p);

    if (undoStack.size() > maxUndo)
        undoStack.erase (undoStack.begin(), undoStack.begin() + static_cast<long> (undoStack.size() - maxUndo));
    cursor = undoStack.size() - 1;
}

void IdeaHistory::addIdea (const Progression& p, const std::string& label)
{
    push (p);
    ideaList.push_front ({ label, p });
    while (ideaList.size() > maxIdeas)
        ideaList.pop_back();
}

std::optional<Progression> IdeaHistory::undo()
{
    if (! canUndo())
        return std::nullopt;
    return undoStack[--cursor];
}

std::optional<Progression> IdeaHistory::redo()
{
    if (! canRedo())
        return std::nullopt;
    return undoStack[++cursor];
}

const Progression* IdeaHistory::current() const
{
    return undoStack.empty() ? nullptr : &undoStack[cursor];
}

util::Json IdeaHistory::ideasToJson() const
{
    util::Json arr;
    arr.asArray();
    for (const auto& idea : ideaList)
    {
        util::Json j;
        j.set ("label", idea.label);
        j.set ("progression", idea.progression.toJson());
        arr.push (std::move (j));
    }
    return arr;
}

void IdeaHistory::ideasFromJson (const util::Json& json)
{
    ideaList.clear();
    for (const auto& j : json.asArray())
    {
        if (auto p = Progression::fromJson (j["progression"]))
            ideaList.push_back ({ j["label"].asString(), *p });
        if (ideaList.size() >= maxIdeas)
            break;
    }
}

} // namespace bounce::gen
