#pragma once

#include "Catalog.h"

namespace wt
{
/** One Type of a (new) Category: the English name used in file names, and its Japanese. */
struct TypeProposal
{
    juce::String english, japanese;

    bool operator== (const TypeProposal&) const = default;
};

struct SimilarCategory
{
    juce::String name, japanese;
    int score = 0;     // 0: nothing in common with the new name; still listed so it can be picked by hand
    int typeCount = 0;
};

/** The Category's own Types in catalogue order (the matrix first), with their Japanese, at most `maxCount`. */
std::vector<TypeProposal> ownTypesOf (const Catalog& catalog, const juce::String& category, int maxCount = Catalog::maxMatrixTypes);

/** The Types every Category has (Category "*"): what a new Category would offer if it got nothing of its own. */
std::vector<TypeProposal> commonTypes (const Catalog& catalog, int maxCount = Catalog::maxMatrixTypes);

/** The Categories that have Types of their own, the ones most like the new name first: words or word starts in common ("TOOLBOXES" / "TOOLS"),
    and Japanese that contains or shares characters with the other ("ガラス食器" / "ガラス"). Names alone cannot tell "CURTAINS" from "CLOTH":
    the list is for picking from, and the first entry is only a suggestion. */
std::vector<SimilarCategory> rankSimilarCategories (const Catalog& catalog, const juce::String& newName, const juce::String& newJapanese);

/** The question to put to a chat AI (Gemini, Claude ...) for the Types of a new Category: what to answer, in what form, the Types that
    exist already (so the spelling stays the same) and a few similar Categories as examples. Meant to be pasted as it is. */
juce::String buildAiPrompt (const Catalog& catalog, const juce::String& newName, const juce::String& newJapanese);

/** Reads the AI's answer. JSON is expected ({"types":[{"en":..,"ja":..}]}, or just the array, or strings like "Hit=叩く"), but the answer may come
    wrapped in a code block or with a sentence before and after, and plain lines ("1. Hit - 叩く") work too. False (with a message) if nothing
    could be read. The result is raw: pass it through tidyProposals. */
bool parseAiReply (const juce::String& reply, std::vector<TypeProposal>& out, juce::String& error);

/** Makes proposals fit the catalogue: English as one PascalCase word of letters and digits (no Japanese, no spaces or symbols), spelled like an
    existing Type of the same name, Japanese filled in from the catalogue if it was left out, no repeats, no blanks, at most `maxCount`. */
std::vector<TypeProposal> tidyProposals (const std::vector<TypeProposal>& proposals, const Catalog& catalog, int maxCount = Catalog::maxMatrixTypes);
} // namespace wt
