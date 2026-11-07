#include "SampleLibrary.h"

using namespace juce;

namespace
{
    const Identifier rootType    { "SampleLibrary" };
    const Identifier foldersType { "Folders" };
    const Identifier folderType  { "Folder" };
    const Identifier samplesType { "Samples" };
    const Identifier sampleType  { "Sample" };
    const Identifier pathID      { "path" };
    const Identifier favID       { "favourite" };
    const Identifier tagsID      { "tags" };
}

SampleLibrary::SampleLibrary() : SampleLibrary (getDefaultStorageFile()) {}

SampleLibrary::SampleLibrary (const File& storageFile) : file (storageFile)
{
    load();
}

File SampleLibrary::getDefaultStorageFile()
{
    auto dir = File::getSpecialLocation (File::userApplicationDataDirectory);
   #if JUCE_MAC
    dir = dir.getChildFile ("Application Support");
   #endif
    return dir.getChildFile ("KickDrum").getChildFile ("SampleLibrary.xml");
}

//==============================================================================
void SampleLibrary::load()
{
    data = {};
    loadedHash = fileHash();

    auto xml = parseXML (file);
    if (xml == nullptr || ! xml->hasTagName (rootType.toString()))
        return;

    const auto tree = ValueTree::fromXml (*xml);
    for (const auto& f : tree.getChildWithName (foldersType))
        if (f.hasType (folderType))
            data.folders.addIfNotAlreadyThere (File (f[pathID].toString()));

    for (const auto& s : tree.getChildWithName (samplesType))
    {
        if (! s.hasType (sampleType) || s[pathID].toString().isEmpty())
            continue;

        SampleInfo info;
        info.favourite = (bool) s[favID];
        info.tags = parseTags (s[tagsID].toString());
        if (! info.isEmpty())
            data.samples[s[pathID].toString()] = info;
    }
}

void SampleLibrary::save()
{
    ValueTree tree (rootType);
    tree.setProperty ("version", 1, nullptr);

    ValueTree folders (foldersType);
    for (auto& f : data.folders)
        folders.appendChild (ValueTree (folderType, { { pathID, f.getFullPathName() } }), nullptr);
    tree.appendChild (folders, nullptr);

    ValueTree samples (samplesType);
    for (auto& [path, info] : data.samples)
    {
        if (info.isEmpty())
            continue;
        ValueTree s (sampleType, { { pathID, path } });
        if (info.favourite)
            s.setProperty (favID, true, nullptr);
        if (! info.tags.isEmpty())
            s.setProperty (tagsID, info.tags.joinIntoString (", "), nullptr);
        samples.appendChild (s, nullptr);
    }
    tree.appendChild (samples, nullptr);

    file.getParentDirectory().createDirectory();
    if (auto xml = tree.createXml())
    {
        // write to a temp file and swap, so a crash never leaves a half-written library
        TemporaryFile temp (file);
        if (xml->writeTo (temp.getFile()))
            temp.overwriteTargetFileWithTemporary();
    }
    loadedHash = fileHash();
}

void SampleLibrary::modify (std::function<void (Data&)> change)
{
    {
        InterProcessLock lock ("KickDrumSampleLibrary");
        const bool locked = lock.enter (2000);   // if another process hangs, still save our change
        load();                                  // pick up other instances' changes first
        change (data);
        save();
        if (locked)
            lock.exit();
    }
    sendChangeMessage();
}

int64 SampleLibrary::fileHash() const
{
    // timestamps can be too coarse (1 s) to see another instance's quick change
    return file.existsAsFile() ? file.loadFileAsString().hashCode64() : 0;
}

bool SampleLibrary::reloadIfChanged()
{
    if (fileHash() == loadedHash)
        return false;
    load();
    sendChangeMessage();
    return true;
}

//==============================================================================
Array<File> SampleLibrary::getFolders() const    { return data.folders; }

void SampleLibrary::addFolder (const File& folder)
{
    modify ([folder] (Data& d) { d.folders.addIfNotAlreadyThere (folder); });
}

void SampleLibrary::removeFolder (const File& folder)
{
    modify ([folder] (Data& d) { d.folders.removeFirstMatchingValue (folder); });
}

bool SampleLibrary::isFavourite (const File& f) const
{
    const auto it = data.samples.find (key (f));
    return it != data.samples.end() && it->second.favourite;
}

void SampleLibrary::setFavourite (const File& f, bool fav)
{
    modify ([k = key (f), fav] (Data& d)
    {
        d.samples[k].favourite = fav;
        if (d.samples[k].isEmpty())
            d.samples.erase (k);
    });
}

StringArray SampleLibrary::getTags (const File& f) const
{
    const auto it = data.samples.find (key (f));
    return it != data.samples.end() ? it->second.tags : StringArray();
}

void SampleLibrary::setTags (const File& f, const StringArray& tags)
{
    modify ([k = key (f), clean = parseTags (tags.joinIntoString (","))] (Data& d)
    {
        d.samples[k].tags = clean;
        if (d.samples[k].isEmpty())
            d.samples.erase (k);
    });
}

void SampleLibrary::addTag (const File& f, const String& tag)
{
    auto tags = getTags (f);
    tags.add (tag);
    setTags (f, tags);
}

void SampleLibrary::removeTag (const File& f, const String& tag)
{
    auto tags = getTags (f);
    tags.removeString (tag, true);
    setTags (f, tags);
}

Array<File> SampleLibrary::getFavourites() const
{
    Array<File> result;
    for (auto& [path, info] : data.samples)
        if (info.favourite)
            result.add (File (path));
    return result;
}

std::vector<std::pair<String, int>> SampleLibrary::getAllTags() const
{
    std::map<String, std::pair<String, int>> counts;   // lower-case key -> (display, count)
    for (auto& [path, info] : data.samples)
        for (auto& t : info.tags)
        {
            auto& entry = counts[t.toLowerCase()];
            if (entry.first.isEmpty())
                entry.first = t;
            ++entry.second;
        }

    std::vector<std::pair<String, int>> result;
    for (auto& [lower, entry] : counts)
        result.push_back (entry);
    return result;
}

StringArray SampleLibrary::parseTags (const String& text)
{
    StringArray result;
    for (auto t : StringArray::fromTokens (text, ",;", "\""))
    {
        t = t.trim().replaceCharacters ("\r\n\t", "   ").trim();
        if (t.isNotEmpty() && ! result.contains (t, true))
            result.add (t.substring (0, 40));
    }
    return result;
}
