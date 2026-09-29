#include "FileCategory.h"
#include <map>

namespace
{
    struct CategoryInfo
    {
        FileCategory category;
        const char* name;
        const char* extensions;     ///< Space separated, lowercase, without dots.
    };

    const CategoryInfo categoryTable[] =
    {
        { FileCategory::folder,     "Folders",     "" },
        { FileCategory::image,      "Images",      "jpg jpeg png gif bmp tif tiff webp heic heif svg ico raw cr2 nef arw dng psd" },
        { FileCategory::audio,      "Audio",       "wav aif aiff flac ogg mp3 m4a aac wma opus mid midi" },
        { FileCategory::video,      "Video",       "mp4 m4v mov avi mkv wmv webm flv mpg mpeg 3gp" },
        { FileCategory::document,   "Documents",   "pdf doc docx xls xlsx ppt pptx odt ods odp rtf epub" },
        { FileCategory::archive,    "Archives",    "zip rar 7z tar gz tgz bz2 xz cab" },
        { FileCategory::executable, "Executables", "exe msi msix appx dmg pkg iso bat cmd ps1 jar apk" },
        { FileCategory::text,       "Text & code", "txt md log csv tsv json xml html htm css js ts ini cfg yaml yml "
                                                   "c cpp h hpp cs java py rb go rs sh jucer sln vcxproj cmake" },
        { FileCategory::other,      "Other",       "" },
    };
}

FileCategory FileCategories::of (const juce::File& file, bool isDirectory)
{
    if (isDirectory)
        return FileCategory::folder;

    static const std::map<juce::String, FileCategory> byExtension = []
    {
        std::map<juce::String, FileCategory> result;

        for (const auto& info : categoryTable)
            for (const auto& extension : juce::StringArray::fromTokens (info.extensions, " ", {}))
                result[extension] = info.category;

        return result;
    }();

    const auto found = byExtension.find (file.getFileExtension().fromFirstOccurrenceOf (".", false, false).toLowerCase());
    return found != byExtension.end() ? found->second : FileCategory::other;
}

juce::String FileCategories::nameOf (FileCategory category)
{
    for (const auto& info : categoryTable)
        if (info.category == category)
            return info.name;

    return {};
}

const std::vector<FileCategory>& FileCategories::all()
{
    static const std::vector<FileCategory> categories = []
    {
        std::vector<FileCategory> result;

        for (const auto& info : categoryTable)
            result.push_back (info.category);

        return result;
    }();

    return categories;
}
