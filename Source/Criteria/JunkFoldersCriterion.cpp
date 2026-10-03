#include "AllCriteria.h"
#include "../Core/FileScanner.h"

namespace
{
    class JunkFolders final : public Criterion
    {
    public:
        JunkFolders()
            : Criterion ({ "junkFolders", "Junk folders",
                           "Folders that can usually be regenerated, such as build outputs and caches. "
                           "Names are matched ignoring case; enable Recursive to find them inside projects.",
                           Trait::recursiveByDefault | Trait::showsFolders },
                        { DefaultSelection::all }) {}

        ParameterSet createParameters() const override
        {
            return { Parameter::text ("names", "Folder names (;)",
                                      "Build;bin;obj;node_modules;__pycache__;.vs;.idea;.gradle;target;DerivedData;.cache;x64;Debug;Release;Builds;JuceLibraryCode") };
        }

        AnalysisResult analyse (const ScanContext& context, const ParameterSet& parameters) const override
        {
            const auto names = parameters.getList ("names");
            const auto isJunk = [&] (const FileEntry& e) { return e.isDirectory && names.contains (e.name(), true); };

            FileScanner::Options options;
            options.shouldDescend = [&] (const FileEntry& e) { return ! isJunk (e); };

            auto entries = FileScanner::scanAndFilter (context, options, isJunk);
            std::sort (entries.begin(), entries.end(), Orderings::largestFirst);

            return AnalysisResult::flat (std::move (entries));
        }
    };
}

std::unique_ptr<Criterion> Criteria::createJunkFolders()   { return std::make_unique<JunkFolders>(); }
