#include "LevelReferences.h"
#include "SampleLibrary.h"

using namespace juce;

LevelReferences::LevelReferences() : LevelReferences (getDefaultStorageFile()) {}

LevelReferences::LevelReferences (const File& f) : file (f)
{
    load();
}

File LevelReferences::getDefaultStorageFile()
{
    return SampleLibrary::getDefaultStorageFile().getSiblingFile ("LevelReferences.xml");
}

void LevelReferences::load()
{
    refs.clear();
    loadedHash = file.existsAsFile() ? file.loadFileAsString().hashCode64() : 0;

    auto xml = parseXML (file);
    if (xml == nullptr || ! xml->hasTagName ("LevelReferences"))
        return;

    for (auto* e : xml->getChildWithTagNameIterator ("Reference"))
    {
        Reference r;
        r.name = e->getStringAttribute ("name").trim();
        if (r.name.isEmpty())
            continue;
        for (int b = 0; b < kNumBands; ++b)
        {
            const auto suffix = String (b + 1);
            r.rmsDb[(size_t) b] = (float) e->getDoubleAttribute ("rms" + suffix, -100.0);
            r.peakDb[(size_t) b] = (float) e->getDoubleAttribute ("peak" + suffix, -100.0);
            r.active[(size_t) b] = e->getBoolAttribute ("active" + suffix, true);
        }
        refs.push_back (r);
    }
}

void LevelReferences::write()
{
    XmlElement root ("LevelReferences");
    for (auto& r : refs)
    {
        auto* e = root.createNewChildElement ("Reference");
        e->setAttribute ("name", r.name);
        for (int b = 0; b < kNumBands; ++b)
        {
            const auto suffix = String (b + 1);
            e->setAttribute ("rms" + suffix, (double) r.rmsDb[(size_t) b]);
            e->setAttribute ("peak" + suffix, (double) r.peakDb[(size_t) b]);
            e->setAttribute ("active" + suffix, r.active[(size_t) b]);
        }
    }

    file.getParentDirectory().createDirectory();
    TemporaryFile temp (file);
    if (root.writeTo (temp.getFile()))
        temp.overwriteTargetFileWithTemporary();
    loadedHash = file.existsAsFile() ? file.loadFileAsString().hashCode64() : 0;
    sendChangeMessage();
}

std::vector<LevelReferences::Reference> LevelReferences::getAll() const
{
    auto sorted = refs;
    std::sort (sorted.begin(), sorted.end(),
               [] (const Reference& a, const Reference& b) { return a.name.compareNatural (b.name) < 0; });
    return sorted;
}

bool LevelReferences::find (const String& name, Reference& out) const
{
    for (auto& r : refs)
        if (r.name.equalsIgnoreCase (name))
        {
            out = r;
            return true;
        }
    return false;
}

void LevelReferences::save (const Reference& ref)
{
    InterProcessLock lock ("KickDrumLevelReferences");
    const bool locked = lock.enter (2000);
    load();   // keep other instances' changes
    auto it = std::find_if (refs.begin(), refs.end(), [&] (const Reference& r) { return r.name.equalsIgnoreCase (ref.name); });
    if (it != refs.end())
        *it = ref;
    else
        refs.push_back (ref);
    write();
    if (locked)
        lock.exit();
}

void LevelReferences::remove (const String& name)
{
    InterProcessLock lock ("KickDrumLevelReferences");
    const bool locked = lock.enter (2000);
    load();
    refs.erase (std::remove_if (refs.begin(), refs.end(), [&] (const Reference& r) { return r.name.equalsIgnoreCase (name); }),
                refs.end());
    write();
    if (locked)
        lock.exit();
}

void LevelReferences::reloadIfChanged()
{
    const auto hash = file.existsAsFile() ? file.loadFileAsString().hashCode64() : 0;
    if (hash != loadedHash)
    {
        load();
        sendChangeMessage();
    }
}
