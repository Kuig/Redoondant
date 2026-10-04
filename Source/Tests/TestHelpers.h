#pragma once

#include "../Core/FileScanner.h"
#include "../Core/Settings.h"
#include "../Criteria/Criterion.h"
#include "../Criteria/Peers.h"
#include "../UI/ResultsModel.h"

/** Helpers shared by the unit tests. */
namespace TestHelpers
{
    /** A temporary folder deleted at the end of a test. */
    struct TempFolder
    {
        TempFolder() : root (juce::File::createTempFile ("RedoondantTest"))    { root.createDirectory(); }
        ~TempFolder()                                                           { root.deleteRecursively(); }

        juce::File write (const juce::String& path, const juce::String& content) const
        {
            const auto file = root.getChildFile (path);
            file.getParentDirectory().createDirectory();
            file.replaceWithText (content);
            return file;
        }

        juce::File root;
    };

    /** A temporary folder with a settings file in it. */
    struct TempSettings : TempFolder
    {
        TempSettings() : file (root.getChildFile ("t.settings"), options()) {}

        SettingsScope scope (const juce::String& prefix)    { return SettingsScope (file, prefix); }

        juce::PropertiesFile file;

    private:
        static juce::PropertiesFile::Options options()
        {
            juce::PropertiesFile::Options o;
            o.filenameSuffix = ".settings";
            return o;
        }
    };

    /** A synthetic entry (nothing is read from the disk). */
    inline FileEntry entryAt (const juce::File& file, juce::int64 size = 0, juce::Time modified = {}, bool folder = false)
    {
        FileEntry e;
        e.file = file;
        e.size = size;
        e.modified = modified;
        e.isDirectory = folder;
        return e;
    }

    /** entryAt() for a name in the temporary directory. */
    inline FileEntry tempEntry (const juce::String& path, juce::int64 size = 0, juce::Time modified = {}, bool folder = false)
    {
        return entryAt (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (path), size, modified, folder);
    }

    /** The names of every item of a list, sorted. */
    inline juce::StringArray namesOf (const ListedResult& result)
    {
        juce::StringArray names;

        for (const auto& group : result.groups)
            for (const auto& item : group.items)
                names.add (item.name());

        names.sort (true);
        return names;
    }

    /** Zips files of `base` (relative paths), optionally all inside a root folder of the archive. */
    inline void writeZip (const juce::File& zip, const juce::File& base, const juce::StringArray& relativePaths, const juce::String& rootName = {})
    {
        juce::ZipFile::Builder builder;

        for (const auto& path : relativePaths)
            builder.addFile (base.getChildFile (path), 9, rootName.isEmpty() ? path : rootName + "/" + path);

        juce::FileOutputStream out (zip);
        builder.writeToStream (out, nullptr);
    }

    /** Runs a criterion with its default parameters (optionally tweaked), then sorts and checks the result
        the way a page does at start: the criterion's default sort and default selection.
    */
    inline ListedResult run (const Criterion& criterion, const juce::File& root, bool recursive = false,
                               const std::function<void (ParameterSet&)>& tweak = {},
                               const std::vector<PeerCriterion>& peers = {})
    {
        ScanContext context;
        context.root = root;
        context.recursive = recursive;

        auto parameters = criterion.createParameters();

        if (tweak != nullptr)
            tweak (parameters);

        ResultsModel model;
        const auto& defaults = criterion.getDefaults();
        model.setDefaultSelection (defaults.selection);
        model.sort (defaults.sortColumn, defaults.sortForwards);
        model.setResult (criterion.analyse (context, parameters, peers), root, criterion.getInfo().grouped);
        return model.getResult();
    }

    inline void setParameter (ParameterSet& parameters, const juce::String& id, const juce::var& value)
    {
        for (auto& p : parameters.all())
            if (p.id == id)
                p.value = value;
    }

    inline const ResultItem* findItem (const ListedGroup& group, const juce::String& name)
    {
        for (const auto& item : group.items)
            if (item.name() == name)
                return &item;

        return nullptr;
    }
}

using namespace TestHelpers;
