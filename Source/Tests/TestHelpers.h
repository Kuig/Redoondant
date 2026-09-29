#pragma once

#include "../Criteria/Criterion.h"

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

    /** Runs a criterion with its default parameters (optionally tweaked). */
    inline AnalysisResult run (const Criterion& criterion, const juce::File& root, bool recursive = false,
                               const std::function<void (ParameterSet&)>& tweak = {})
    {
        ScanContext context;
        context.root = root;
        context.recursive = recursive;

        auto parameters = criterion.createParameters();

        if (tweak != nullptr)
            tweak (parameters);

        return criterion.analyse (context, parameters);
    }

    inline void setParameter (ParameterSet& parameters, const juce::String& id, const juce::var& value)
    {
        for (auto& p : parameters.all())
            if (p.id == id)
                p.value = value;
    }

    inline const FileEntry* findItem (const ResultGroup& group, const juce::String& name)
    {
        for (const auto& item : group.items)
            if (item.name() == name)
                return &item;

        return nullptr;
    }
}

using namespace TestHelpers;
