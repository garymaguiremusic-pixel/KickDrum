#include "FolderScanner.h"

using namespace juce;

FolderScanner::FolderScanner() : Thread ("KickDrum folder scan") {}

FolderScanner::~FolderScanner()
{
    *alive = false;
    stopThread (4000);
}

void FolderScanner::scan (const Array<File>& roots, const String& wildcard, bool recursive)
{
    cancel();
    {
        const ScopedLock sl (lock);
        pendingRoots = roots;
        pendingWildcard = wildcard;
        pendingRecursive = recursive;
    }
    startThread (Thread::Priority::low);
}

void FolderScanner::cancel()
{
    stopThread (4000);
}

void FolderScanner::run()
{
    Array<File> roots;
    String wildcard;
    bool recursive = true;
    {
        const ScopedLock sl (lock);
        roots = pendingRoots;
        wildcard = pendingWildcard;
        recursive = pendingRecursive;
    }

    bool truncated = false;
    Array<File> files, folders;
    auto shouldStop = [this] { return threadShouldExit(); };
    if (recursive)
    {
        files = findAudioFiles (roots, wildcard, shouldStop, truncated);
    }
    else
    {
        for (auto& root : roots)
        {
            Array<File> f, d;
            bool t = false;
            listFolder (root, wildcard, shouldStop, f, d, t);
            files.addArray (f);
            folders.addArray (d);
            truncated = truncated || t;
        }
    }
    if (threadShouldExit())
        return;

    MessageManager::callAsync ([this, files, folders, truncated, alive = alive]
    {
        if (*alive && onFinished)
            onFinished (files, folders, truncated);
    });
}

Array<File> FolderScanner::findAudioFiles (const Array<File>& roots, const String& wildcard,
                                           std::function<bool()> shouldStop, bool& truncated)
{
    Array<File> files;
    truncated = false;

    for (auto& root : roots)
    {
        if (! root.isDirectory())
            continue;

        for (const auto& entry : RangedDirectoryIterator (root, true, wildcard, File::findFiles,
                                                          File::FollowSymlinks::noCycles))
        {
            if (shouldStop && shouldStop())
                return {};

            // hidden files, and dot-files such as macOS "._name.wav" resource forks on shared drives
            if (entry.isHidden() || entry.getFile().getFileName().startsWithChar ('.'))
                continue;

            files.addIfNotAlreadyThere (entry.getFile());
            if (files.size() >= kMaxFiles)
            {
                truncated = true;
                break;
            }
        }
    }

    struct Sorter
    {
        static int compareElements (const File& a, const File& b)
        {
            return a.getFullPathName().compareNatural (b.getFullPathName());
        }
    } sorter;
    files.sort (sorter);
    return files;
}

void FolderScanner::listFolder (const File& dir, const String& wildcard, std::function<bool()> shouldStop,
                               Array<File>& files, Array<File>& folders, bool& truncated)
{
    files.clear();
    folders.clear();
    truncated = false;
    if (! dir.isDirectory())
        return;

    auto skip = [] (const DirectoryEntry& e)
    {
        return e.isHidden() || e.getFile().getFileName().startsWithChar ('.');
    };

    for (const auto& entry : RangedDirectoryIterator (dir, false, "*", File::findDirectories))
    {
        if (shouldStop && shouldStop())
            return;
        if (! skip (entry))
            folders.add (entry.getFile());
    }

    for (const auto& entry : RangedDirectoryIterator (dir, false, wildcard, File::findFiles))
    {
        if (shouldStop && shouldStop())
            return;
        if (skip (entry))
            continue;
        files.add (entry.getFile());
        if (files.size() >= kMaxFiles)
        {
            truncated = true;
            break;
        }
    }

    struct ByName
    {
        static int compareElements (const File& a, const File& b)
        {
            return a.getFileName().compareNatural (b.getFileName());
        }
    } byName;
    folders.sort (byName);
    files.sort (byName);
}

//==============================================================================
Array<File> applyBrowserFilter (const Array<File>& files, const BrowserFilter& filter, const SampleLibrary& library)
{
    const auto words = StringArray::fromTokens (filter.query.toLowerCase(), " ", "\"");
    Array<File> result;

    for (auto& f : files)
    {
        if (filter.favouritesOnly && ! library.isFavourite (f))
            continue;

        const auto tags = library.getTags (f);
        if (filter.tag.isNotEmpty() && ! tags.contains (filter.tag, true))
            continue;

        if (! words.isEmpty())
        {
            const auto haystack = (f.getFileName() + " " + tags.joinIntoString (" ")).toLowerCase();
            bool all = true;
            for (auto& w : words)
                if (w.isNotEmpty() && ! haystack.contains (w))
                    all = false;
            if (! all)
                continue;
        }

        result.add (f);
    }
    return result;
}
