#include "AllCriteria.h"
#include "../Core/FileScanner.h"
#include "../Core/Paths.h"
#include <set>

namespace
{
    class JunkFolders final : public Criterion
    {
    public:
        JunkFolders()
            : Criterion ({ "junkFolders", "Junk folders",
                           "Folders that can usually be regenerated, such as build outputs and caches. "
                           "Names are matched ignoring case; enable Recursive to find them inside projects. Generic names "
                           "(Release, bin...) count only in a folder holding a project file (.sln, CMakeLists.txt, package.json...).",
                           Trait::recursiveByDefault | Trait::showsFolders },
                        { DefaultSelection::all }) {}

        ParameterSet createParameters() const override
        {
            return { Parameter::text ("names", "Folder names (;)",
                                      "Build;bin;obj;node_modules;__pycache__;.vs;.idea;.gradle;target;DerivedData;.cache;x64;Debug;Release;Builds;JuceLibraryCode"),
                     Parameter::textWithToggle ("projectOnly", "Only next to a project file (;)", "Build;bin;obj;target;x64;Debug;Release;Builds", true) };
        }

        AnalysisResult analyse (const ScanContext& context, const ParameterSet& parameters) const override
        {
            const auto names = parameters.getList ("names");
            const auto generic = parameters.isEnabled ("projectOnly") ? parameters.getList ("projectOnly") : juce::StringArray();
            const auto matchesName = [&] (const FileEntry& e) { return e.isDirectory && names.contains (e.name(), true); };

            FileScanner::Options options;
            options.shouldDescend = [&] (const FileEntry& e) { return ! matchesName (e); };

            auto entries = FileScanner::scan (context, options);

            // A generic name (Release, bin...) is only a build output in a project: a folder holding a project file.
            // The parent of a listed folder was walked, so its files are in the scan.
            std::set<juce::String> projects;

            for (const auto& e : entries)
                if (FileScanner::isProjectFile (e.name()))
                    projects.insert (Paths::key (e.file.getParentDirectory()));

            return AnalysisResult::flatSorted (FileScanner::keepIf (std::move (entries), [&] (const FileEntry& e)
                                                                    {
                                                                        return matchesName (e)
                                                                            && (! generic.contains (e.name(), true)
                                                                                || projects.count (Paths::key (e.file.getParentDirectory())) > 0);
                                                                    }),
                                               Orderings::largestFirst);
        }
    };
}

std::unique_ptr<Criterion> Criteria::createJunkFolders()   { return std::make_unique<JunkFolders>(); }
