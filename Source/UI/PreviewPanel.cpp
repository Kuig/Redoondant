#include "PreviewPanel.h"
#include "../Archives/ArchiveReader.h"
#include "../Core/FileCategory.h"
#include "../Core/Format.h"
#include "../Metadata/MetadataDiff.h"
#include "../Metadata/MetadataReader.h"
#include "../Pdf/PdfDocument.h"
#include "../Platform/ComInit.h"
#include "../Platform/ShellIcon.h"
#include "../Platform/ShellThumbnail.h"

namespace
{
    constexpr juce::int64 maxImageBytes = 64 * 1024 * 1024;
    constexpr int maxTextBytes = 64 * 1024;
    constexpr int maxListedItems = 500;
    constexpr int renderPixels = 800;
    constexpr int iconPixels = 128;
    constexpr int maxTiles = 12;
    const juce::String pathKey ("Base.Path");

    void setUpReadOnly (juce::TextEditor& editor)
    {
        editor.setMultiLine (true);
        editor.setReadOnly (true);
        editor.setScrollbarsShown (true);
        editor.setCaretVisible (false);
    }

    /** True if the start of the file looks like text (no NUL bytes). */
    bool looksLikeText (const juce::File& file)
    {
        juce::FileInputStream in (file);

        if (! in.openedOk())
            return false;

        char buffer[4096];
        const int read = in.read (buffer, (int) sizeof (buffer));
        return read > 0 && std::memchr (buffer, 0, (size_t) read) == nullptr;
    }

    juce::String readTextStart (const juce::File& file)
    {
        juce::FileInputStream in (file);
        juce::MemoryBlock block;
        in.readIntoMemoryBlock (block, maxTextBytes);

        auto content = block.toString();

        if (file.getSize() > maxTextBytes)
            content << "\n\n[... truncated: showing the first " << Format::size (maxTextBytes) << "]";

        return content;
    }

    /** Sorted, truncated listing. */
    juce::String listing (juce::StringArray names)
    {
        names.sortNatural();

        if (names.size() > maxListedItems)
        {
            names.removeRange (maxListedItems, names.size());
            names.add ("...");
        }

        return names.joinIntoString ("\n");
    }

    juce::String listFolder (const juce::File& folder)
    {
        juce::StringArray names;

        for (const auto& item : juce::RangedDirectoryIterator (folder, false, "*", juce::File::findFilesAndDirectories))
            names.add (item.getFile().getFileName() + (item.isDirectory() ? "/" : ""));

        return listing (names);
    }

    void addBaseDetails (Metadata& details, const FileEntry& entry)
    {
        details.add (pathKey, "Path", entry.file.getFullPathName());
        details.add ("Base.Type", "Type", entry.isDirectory ? juce::String ("Folder") : FileCategories::nameOf (FileCategories::of (entry)));
        details.add ("Base.Size", "Size", Format::size (entry.size) + " (" + juce::String (entry.size) + " bytes)");

        if (entry.isDirectory)
            details.add ("Base.Files", "Files", juce::String (entry.fileCount));

        details.add ("Base.Created", "Created", Format::date (entry.created));
        details.add ("Base.Modified", "Modified", Format::date (entry.modified));
        details.add ("Base.ReadOnly", "Read-only", entry.file.hasWriteAccess() ? "No" : "Yes");
        details.add ("Base.Hidden", "Hidden", entry.file.isHidden() ? "Yes" : "No");
    }

    /** Builds the preview of an entry. Runs on the loader thread (COM initialised). */
    PreviewPanel::Data load (const FileEntry& entry, juce::AudioFormatManager& formats, bool withAudioPlayer)
    {
        using Content = PreviewPanel::Content;

        PreviewPanel::Data data;
        data.name = entry.name();
        addBaseDetails (data.details, entry);

        const auto& file = entry.file;
        const auto category = FileCategories::of (entry);

        if (entry.isDirectory)
        {
            data.text = listFolder (file);

            if (data.text.isEmpty())
                data.image = ShellIcon::get (file, iconPixels);     // An empty folder: show its icon rather than nothing.

            data.content = data.text.isNotEmpty() ? Content::text : (data.image.isValid() ? Content::image : Content::none);
            return data;
        }

        data.details.append (MetadataReader::read (file));

        if (ArchiveReader::isArchive (file))
        {
            if (const auto entries = ArchiveReader::list (file, {}))
            {
                juce::StringArray names;
                juce::int64 total = 0;

                for (const auto& e : *entries)
                {
                    names.add (e.path + "  (" + Format::size (e.size) + ")");
                    total += e.size;
                }

                data.details.add ({}, "Archived files", juce::String ((int) entries->size()));
                data.details.add ({}, "Uncompressed size", Format::size (total));
                data.content = Content::text;
                data.text = listing (names);
            }
            else
            {
                data.details.add ({}, "Archive", "Unreadable or encrypted");
            }

            return data;
        }

        if (category == FileCategory::image && entry.size <= maxImageBytes)
            data.image = juce::ImageFileFormat::loadFrom (file);
        else if (PdfDocument::isPdf (file))
            data.image = PdfDocument::renderPage (file, 0, renderPixels);

        if (withAudioPlayer && data.image.isNull() && category == FileCategory::audio)
        {
            data.audio.reset (formats.createReaderFor (file));

            if (data.audio != nullptr)
            {
                data.content = Content::audio;
                const auto format = AudioPlayer::describe (*data.audio);

                for (const auto& key : format.getAllKeys())
                    if (key != "Duration" || data.details.find (MetadataKeys::duration) == nullptr)    // Shown once.
                        data.details.add ("Audio." + key, key, format[key]);
            }
        }
        else if (data.image.isNull() && (category == FileCategory::text || category == FileCategory::other) && looksLikeText (file))
            data.text = readTextStart (file);
        else if (data.image.isNull())
            data.image = ShellThumbnail::get (file, renderPixels);

        // Nothing to preview: show the file's icon (its own, for executables) instead of leaving the area blank.
        if (data.image.isNull() && data.text.isEmpty() && data.content == Content::none)
            data.image = ShellIcon::get (file, iconPixels);

        if (data.content == Content::none)
            data.content = data.image.isValid() ? Content::image : (data.text.isNotEmpty() ? Content::text : Content::none);

        return data;
    }
}

/** One preview of a multiple selection: the file's name over its image or text. */
class PreviewPanel::Tile final : public juce::Component
{
public:
    explicit Tile (const Data& data)
    {
        name.setText (data.name, juce::dontSendNotification);
        name.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        name.setMinimumHorizontalScale (0.7f);
        image.setImagePlacement (juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize);
        image.setImage (data.image);
        text.setText (data.text);

        addAndMakeVisible (name);
        addChildComponent (image);
        addChildComponent (text);
        image.setVisible (data.content == Content::image);
        text.setVisible (data.content == Content::text);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        name.setBounds (area.removeFromTop (18));
        image.setBounds (area);
        text.setBounds (area);
    }

private:
    juce::Label name;
    juce::ImageComponent image;
    TextPreview text;
};

PreviewPanel::PreviewPanel (SettingsScope settings)
    : audio (settings.child ("audio"))
{
    formats.registerBasicFormats();

    title.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    title.setMinimumHorizontalScale (0.6f);
    image.setImagePlacement (juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize);
    setUpReadOnly (metadata);

    addAndMakeVisible (title);
    addChildComponent (image);
    addChildComponent (text);
    addChildComponent (audio);
    addAndMakeVisible (metadata);

    show ({});

    // Opening the audio output can take seconds: start in the background right away.
    audio.prepareDevice();
}

PreviewPanel::~PreviewPanel()
{
    loader.removeAllJobs (true, 5000);
}

void PreviewPanel::show (std::vector<FileEntry> entries)
{
    const int request = ++generation;
    loader.removeAllJobs (false, 0);    // Drop queued requests; a running one finishes and is ignored.

    audio.unload();
    entries.erase (std::remove_if (entries.begin(), entries.end(), [] (const FileEntry& e) { return ! e.file.exists(); }), entries.end());

    if (entries.empty())
    {
        title.setText ("No selection", juce::dontSendNotification);
        display ({});
        return;
    }

    const int notShown = juce::jmax (0, (int) entries.size() - maxTiles);
    entries.resize (std::min (entries.size(), (size_t) maxTiles));
    const bool single = entries.size() == 1;

    title.setText (single ? entries.front().name() + "  (loading...)" : juce::String (entries.size() + (size_t) notShown) + " items selected  (loading...)",
                   juce::dontSendNotification);

    loader.addJob ([safeThis = juce::Component::SafePointer<PreviewPanel> (this), &formats = formats, request, single, notShown, items = std::move (entries)]
    {
        const ScopedComInit com;
        std::vector<Data> all;

        for (const auto& item : items)
            all.push_back (load (item, formats, single));

        juce::MessageManager::callAsync ([safeThis, request, single, notShown, all = std::move (all)]() mutable
        {
            if (safeThis == nullptr || safeThis->generation != request)
                return;

            if (single)
            {
                safeThis->title.setText (all.front().name, juce::dontSendNotification);
                safeThis->display (std::move (all.front()));
            }
            else
            {
                safeThis->title.setText (juce::String ((int) all.size() + notShown) + " items selected", juce::dontSendNotification);
                safeThis->displayMany (std::move (all), notShown);
            }
        });
    });
}

void PreviewPanel::display (Data data)
{
    multiple = false;
    tiles.clear();

    if (data.content == Content::audio)
        audio.load (data.audio);

    content = data.content;
    image.setImage (data.image);
    text.setText (data.text);

    image.setVisible (content == Content::image);
    text.setVisible (content == Content::text);
    audio.setVisible (content == Content::audio);
    showDetails (data.details);
    resized();
}

void PreviewPanel::displayMany (std::vector<Data> all, int notShown)
{
    multiple = true;
    content = Content::none;
    image.setVisible (false);
    text.setVisible (false);
    audio.setVisible (false);

    tiles.clear();

    for (const auto& data : all)
    {
        tiles.push_back (std::make_unique<Tile> (data));
        addAndMakeVisible (*tiles.back());
    }

    // Per file: its path, then only the properties that differ from the other files.
    std::vector<Metadata> details;

    for (const auto& data : all)
        details.push_back (data.details);

    const auto differing = MetadataDiff::differing (details);
    juce::String lines;
    bool anyDifference = false;

    for (size_t i = 0; i < all.size(); ++i)
    {
        lines << all[i].details.text (pathKey) << "\n";

        for (const auto& item : differing[i].items())
        {
            if (item.key == pathKey)
                continue;

            lines << "    " << item.label << ":  " << item.value << "\n";
            anyDifference = true;
        }

        lines << "\n";
    }

    if (notShown > 0)
        lines << "... and " << notShown << " more not shown.\n\n";

    if (! anyDifference)
        lines << "All other properties are identical.";

    metadata.setText (lines.trimEnd(), false);
    resized();
}

void PreviewPanel::showDetails (const Metadata& details)
{
    juce::String lines;

    for (const auto& item : details.items())
        lines << item.label << ":  " << item.value << "\n";

    metadata.setText (lines.trimEnd(), false);
}

void PreviewPanel::paint (juce::Graphics& g)
{
    g.fillAll (findColour (juce::ResizableWindow::backgroundColourId).darker (0.1f));
}

void PreviewPanel::resized()
{
    auto area = getLocalBounds().reduced (10);
    title.setBounds (area.removeFromTop (28));
    area.removeFromTop (6);

    auto preview = area.removeFromTop (content == Content::audio ? 86 : area.getHeight() * 55 / 100);

    for (auto* c : std::initializer_list<juce::Component*> { &image, &text, &audio })
        c->setBounds (preview);

    if (multiple && ! tiles.empty())
    {
        // A grid as square as the number of tiles allows.
        const int columns = (int) std::ceil (std::sqrt ((double) tiles.size()));
        const int rows = ((int) tiles.size() + columns - 1) / columns;
        const int cellWidth = preview.getWidth() / columns, cellHeight = preview.getHeight() / rows;

        for (size_t i = 0; i < tiles.size(); ++i)
            tiles[i]->setBounds (preview.getX() + ((int) i % columns) * cellWidth, preview.getY() + ((int) i / columns) * cellHeight,
                                 cellWidth, cellHeight);
    }

    area.removeFromTop (8);
    metadata.setBounds (area);
}
