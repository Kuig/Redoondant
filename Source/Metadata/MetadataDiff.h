#pragma once

#include "Metadata.h"
#include <map>

/** Compares the metadata of several files, to show only what tells them apart. */
namespace MetadataDiff
{
    /** For each file, the items whose value is not the same in all the files (an item that is
        missing in some files counts as different). Items without a key are never shared, so they are kept.
        Each file's items keep their own order.
    */
    inline std::vector<Metadata> differing (const std::vector<Metadata>& files)
    {
        std::map<juce::String, std::vector<juce::String>> valuesByKey;    // Per key: the value in each file ("" + flag when missing).
        std::map<juce::String, std::vector<bool>> presentByKey;

        for (size_t f = 0; f < files.size(); ++f)
            for (const auto& item : files[f].items())
            {
                if (item.key.isEmpty())
                    continue;

                auto& values = valuesByKey[item.key];
                auto& present = presentByKey[item.key];
                values.resize (files.size());
                present.resize (files.size());
                values[f] = item.value;
                present[f] = true;
            }

        const auto isShared = [&] (const juce::String& key)
        {
            const auto& values = valuesByKey.at (key);
            const auto& present = presentByKey.at (key);

            for (size_t f = 0; f < files.size(); ++f)
                if (! present[f] || values[f] != values.front())
                    return false;

            return true;
        };

        std::vector<Metadata> result (files.size());

        for (size_t f = 0; f < files.size(); ++f)
            for (const auto& item : files[f].items())
                if (item.key.isEmpty() || ! isShared (item.key))
                    result[f].items().push_back (item);

        return result;
    }
}
