#pragma once

#include <juce_core/juce_core.h>

namespace wt
{
struct LinkItem
{
    juce::File source, target;
};

struct LinkPlan
{
    juce::File root;
    std::vector<LinkItem> items;
    juce::StringArray problems; // reasons the plan cannot (fully) run
};

struct LinkRunResult
{
    int created = 0, alreadyThere = 0;
    juce::StringArray errors;
};

struct UndoResult
{
    int removed = 0, kept = 0;
    juce::StringArray notes;
};

struct AssignedFile
{
    juce::File file;
    juce::String category, type;
};

/**
    An "organised view" of legacy files as NTFS hard links: <root>/<Category>/<Type>/<Category>_<Type>NN.ext.
    A hard link is a second name for the same data (no extra disk space), so the legacy paths that Cubase projects
    use keep working untouched, and removing a link never removes the original. Both must be on the same volume.
*/
LinkPlan planLinks (const juce::File& outputRoot, std::vector<AssignedFile> assigned);

bool sameVolume (const juce::File& a, const juce::File& b);
/** True if both names point at the same data (the same file index on the same volume). */
bool isSameFile (const juce::File& a, const juce::File& b);
/** Number of names (hard links) a file has; 0 if it cannot be queried. */
int linkCount (const juce::File& f);

/** Creates the links; never overwrites. Created links are listed in `manifest` so they can be undone. */
LinkRunResult createLinks (const LinkPlan& plan, const juce::File& manifest, const std::function<bool (int, int)>& progress = {});

/** Deletes only the links named in the manifest, and only if they are still links to their original. */
UndoResult undoLinks (const juce::File& manifest);
} // namespace wt
