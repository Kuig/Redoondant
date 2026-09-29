#include "AllCriteria.h"
#include "../Core/FileScanner.h"
#include "../Core/Grouping.h"

namespace
{
    class JunkFolders final : public Criterion
    {
    public:
        JunkFolders()
            : Criterion ({ "junkFolders", "Junk folders",
                           "Folders that can usually be regenerated, such as build outputs and caches. "
                           "Names are matched ignoring case; enable Recursive to find them inside projects.",
                           false }) {}

        ParameterSet createParameters() const override
        {
            return { Parameter::text ("names", "Folder names (;)",
                                      "Build;bin;obj;node_modules;__pycache__;.vs;.idea;.gradle;target;DerivedData;.cache;x64;Debug;Release") };
        }

        AnalysisResult analyse (const ScanContext& context, const ParameterSet& parameters) const override
        {
            const auto names = parameters.getList ("names");
            const auto isJunk = [&] (const FileEntry& e) { return e.isDirectory && names.contains (e.name(), true); };

            FileScanner::Options options;
            options.shouldDescend = [&] (const FileEntry& e) { return ! isJunk (e); };

            auto entries = FileScanner::scan (context, options);
            entries.erase (std::remove_if (entries.begin(), entries.end(), [&] (const FileEntry& e) { return ! isJunk (e); }), entries.end());
            std::sort (entries.begin(), entries.end(), [] (const FileEntry& a, const FileEntry& b) { return a.size > b.size; });

            Grouping::selectAll (entries, true);
            return AnalysisResult::flat (std::move (entries));
        }
    };
}

std::unique_ptr<Criterion> Criteria::createJunkFolders()   { return std::make_unique<JunkFolders>(); }
