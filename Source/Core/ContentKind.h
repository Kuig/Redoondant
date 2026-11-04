#pragma once

#include "ScanContext.h"
#include <array>
#include <map>

struct FileEntry;

/** What an item is, judged from what it holds (the files of a folder or an archive): a project of source code, a
    collection of pictures, a pack of samples... Unlike FileCategory, which only looks at a file's extension.
*/
enum class ContentKind
{
    unknown,        ///< Not classified yet.
    sourceCode,
    application,
    audioPlugin,
    font,
    pictures,
    graphics,
    video,
    music,
    samples,
    mediaProject,
    documents,
    data,
    other           ///< Nothing recognised in it.
};

namespace ContentKinds
{
    juce::String nameOf (ContentKind kind);

    /** Every kind but `unknown`, in display order (also the order that wins a tie). */
    const std::vector<ContentKind>& all();

    /** The kind of one file from its name and size: `relativePath` ('/' or '\' separated) may start with the folders
        holding it, whose names count for telling samples from music. `other` if the file is not recognised.
    */
    ContentKind ofFile (const juce::String& relativePath, juce::int64 size);

    /** Classifies items by their content. Each file recognised counts one point for its kind; a marker (a project file,
        a `.git` folder, an installer at most one level down, a plugin bundle, a project of a media application) gives its
        kind more points than the files examined, so it decides unless another kind has one too. The highest score wins,
        ties go to the kind that comes first in all(); nothing recognised is `other`.

        Folders are read one level at a time and their tallies are kept for the life of the classifier, so classifying a
        folder and then one inside it reads nothing twice. Archives are listed, archives inside them are not opened.
        Not thread-safe: use one per thread.
    */
    class Classifier
    {
    public:
        /** The context only tells when to stop (see ScanContext::isCancelled). */
        explicit Classifier (ScanContext context);

        ContentKind of (const FileEntry& entry);

        /** What has been counted in an item. */
        struct Tally
        {
            static constexpr size_t kinds = (size_t) ContentKind::other + 1;

            std::array<int, kinds> points {};
            std::array<bool, kinds> marked {};
            int files = 0;                  ///< Files examined, recognised or not.
            bool installerAtTop = false;    ///< An installer directly inside.
            bool installerBelow = false;    ///< An installer one level down.

            /** Adds the content of a folder held directly inside. */
            void addFolder (const Tally& folder);

            ContentKind winner() const;
        };

    private:
        ScanContext context;
        std::map<juce::String, Tally> folders;      // Paths::key -> tally of the folder's whole content.

        const Tally& folderTally (const juce::File& folder);
        Tally archiveTally (const juce::File& archive);

        JUCE_DECLARE_NON_COPYABLE (Classifier)
    };
}
