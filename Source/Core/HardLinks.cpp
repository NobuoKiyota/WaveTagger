#include "HardLinks.h"
#include "Persistence.h"

#if JUCE_WINDOWS
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

namespace wt
{
namespace
{
juce::String clean (juce::String s)
{
    for (const char* c = "\\/:*?\"<>|"; *c != 0; ++c)
        s = s.replaceCharacter ((juce::juce_wchar) *c, '_');
    s = s.trim();
    return s.isEmpty() ? juce::String ("Unsorted") : s;
}

juce::File existingAncestor (juce::File f)
{
    while (! f.exists() && f.getParentDirectory() != f)
        f = f.getParentDirectory();
    return f;
}

#if JUCE_WINDOWS
bool fileInfo (const juce::File& f, BY_HANDLE_FILE_INFORMATION& info)
{
    const auto h = CreateFileW (f.getFullPathName().toWideCharPointer(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    const auto ok = GetFileInformationByHandle (h, &info) != 0;
    CloseHandle (h);
    return ok;
}
#endif
} // namespace

LinkPlan planLinks (const juce::File& outputRoot, std::vector<AssignedFile> assigned)
{
    LinkPlan plan;
    plan.root = outputRoot;

    std::sort (assigned.begin(), assigned.end(), [] (const AssignedFile& a, const AssignedFile& b) {
        return a.file.getFullPathName().compareIgnoreCase (b.file.getFullPathName()) < 0;
    });

    std::map<juce::String, int> counts;
    for (const auto& a : assigned)
        ++counts[clean (a.category) + "/" + clean (a.type)];

    std::map<juce::String, int> next;
    for (const auto& a : assigned)
    {
        if (! a.file.existsAsFile())
        {
            plan.problems.add ("missing: " + a.file.getFullPathName());
            continue;
        }

        const auto cat = clean (a.category), type = clean (a.type);
        const auto key = cat + "/" + type;
        const auto number = ++next[key];
        const auto width = counts[key] > 99 ? 3 : 2;
        const auto name = cat + "_" + type + juce::String (number).paddedLeft ('0', width) + a.file.getFileExtension();
        plan.items.push_back ({ a.file, outputRoot.getChildFile (cat).getChildFile (type).getChildFile (name) });
    }

    if (! plan.items.empty() && ! sameVolume (plan.items.front().source, outputRoot))
        plan.problems.add ("The output folder must be on the same drive as the originals (hard links cannot cross drives).");

    return plan;
}

bool sameVolume (const juce::File& a, const juce::File& b)
{
    const auto x = existingAncestor (a), y = existingAncestor (b);
#if JUCE_WINDOWS
    BY_HANDLE_FILE_INFORMATION ia {}, ib {};
    return fileInfo (x, ia) && fileInfo (y, ib) && ia.dwVolumeSerialNumber == ib.dwVolumeSerialNumber;
#else
    return x.getVolumeSerialNumber() == y.getVolumeSerialNumber();
#endif
}

bool isSameFile (const juce::File& a, const juce::File& b)
{
#if JUCE_WINDOWS
    BY_HANDLE_FILE_INFORMATION ia {}, ib {};
    return fileInfo (a, ia) && fileInfo (b, ib) && ia.dwVolumeSerialNumber == ib.dwVolumeSerialNumber
           && ia.nFileIndexHigh == ib.nFileIndexHigh && ia.nFileIndexLow == ib.nFileIndexLow;
#else
    return a == b;
#endif
}

int linkCount (const juce::File& f)
{
#if JUCE_WINDOWS
    BY_HANDLE_FILE_INFORMATION info {};
    return fileInfo (f, info) ? (int) info.nNumberOfLinks : 0;
#else
    juce::ignoreUnused (f);
    return 0;
#endif
}

LinkRunResult createLinks (const LinkPlan& plan, const juce::File& manifest, const std::function<bool (int, int)>& progress)
{
    LinkRunResult result;
    if (! plan.problems.isEmpty() && plan.items.empty())
    {
        result.errors = plan.problems;
        return result;
    }

    for (const auto& p : plan.problems)
        if (p.startsWith ("The output folder"))
        {
            result.errors.add (p);
            return result; // nothing can be linked across drives
        }

    // keep what an earlier run created, so one "undo" covers everything
    juce::Array<juce::var> created;
    const auto previous = readJson (manifest);
    if (const auto* arr = previous.getProperty ("created", {}).getArray())
        for (const auto& v : *arr)
            created.add (v);

    const auto writeManifest = [&] {
        auto* root = new juce::DynamicObject();
        root->setProperty ("root", plan.root.getFullPathName());
        root->setProperty ("created", created);
        writeJson (manifest, juce::var (root));
    };

    int index = 0;
    for (const auto& item : plan.items)
    {
        if (progress && ! progress (index++, (int) plan.items.size()))
            break;

        if (item.target.exists())
        {
            if (isSameFile (item.source, item.target))
                ++result.alreadyThere;
            else
                result.errors.add ("name already taken by a different file: " + item.target.getFullPathName());
            continue;
        }

        if (! item.target.getParentDirectory().createDirectory().wasOk())
        {
            result.errors.add ("cannot create folder for " + item.target.getFullPathName());
            continue;
        }

#if JUCE_WINDOWS
        const auto ok = CreateHardLinkW (item.target.getFullPathName().toWideCharPointer(), item.source.getFullPathName().toWideCharPointer(), nullptr) != 0;
#else
        const auto ok = false;
#endif
        if (! ok)
        {
            result.errors.add ("link failed: " + item.target.getFullPathName());
            continue;
        }

        auto* o = new juce::DynamicObject();
        o->setProperty ("source", item.source.getFullPathName());
        o->setProperty ("target", item.target.getFullPathName());
        created.add (juce::var (o));
        ++result.created;

        if (result.created % 200 == 0)
            writeManifest();
    }

    writeManifest();
    return result;
}

UndoResult undoLinks (const juce::File& manifest)
{
    UndoResult result;
    const auto json = readJson (manifest); // keep the parsed value alive while its array is used
    const auto* arr = json.getProperty ("created", {}).getArray();
    if (arr == nullptr)
    {
        result.notes.add ("no manifest found");
        return result;
    }

    const juce::File root (json.getProperty ("root", "").toString());
    juce::Array<juce::var> remaining;
    std::set<juce::String> parents;

    for (const auto& v : *arr)
    {
        const juce::File source (v.getProperty ("source", "").toString());
        const juce::File target (v.getProperty ("target", "").toString());

        if (! target.exists())
        {
            ++result.removed; // already gone: nothing left to undo, but its folder may now be empty
            parents.insert (target.getParentDirectory().getFullPathName());
            continue;
        }

        if (source.existsAsFile() && isSameFile (source, target) && linkCount (target) >= 2)
        {
            if (target.deleteFile())
            {
                ++result.removed;
                parents.insert (target.getParentDirectory().getFullPathName());
                continue;
            }
        }

        ++result.kept;
        result.notes.add ("left alone (no longer a link to its original): " + target.getFullPathName());
        remaining.add (v);
    }

    // remove folders that became empty, but never above the output root
    for (auto dir : parents)
    {
        juce::File d (dir);
        while (root.getFullPathName().isNotEmpty() && d != root && d.isAChildOf (root) && d.isDirectory()
               && d.findChildFiles (juce::File::findFilesAndDirectories, false, "*").isEmpty())
        {
            d.deleteFile();
            d = d.getParentDirectory();
        }
    }

    if (remaining.isEmpty())
    {
        manifest.deleteFile();
    }
    else
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("root", root.getFullPathName());
        o->setProperty ("created", remaining);
        writeJson (manifest, juce::var (o));
    }
    return result;
}
} // namespace wt
