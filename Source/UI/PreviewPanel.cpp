#include "PreviewPanel.h"
#include "../Archives/ArchiveReader.h"
#include "../Core/FileCategory.h"
#include "../Core/Format.h"
#include "../Metadata/MetadataReader.h"
#include "../Pdf/PdfDocument.h"
#include "../Platform/ComInit.h"
#include "../Platform/ShellThumbnail.h"

namespace
{
    constexpr juce::int64 maxImageBytes = 64 * 1024 * 1024;
    constexpr int maxTextBytes = 64 * 1024;
    constexpr int maxListedItems = 500;
    constexpr int renderPixels = 800;

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
        details.add ({}, "Path", entry.file.getFullPathName());
        details.add ({}, "Type", entry.isDirectory ? juce::String ("Folder") : FileCategories::nameOf (FileCategories::of (entry)));
        details.add ({}, "Size", Format::size (entry.size) + " (" + juce::String (entry.size) + " bytes)");

        if (entry.isDirectory)
            details.add ({}, "Files", juce::String (entry.fileCount));

        details.add ({}, "Created", Format::date (entry.created));
        details.add ({}, "Modified", Format::date (entry.modified));
        details.add ({}, "Read-only", entry.file.hasWriteAccess() ? "No" : "Yes");
        details.add ({}, "Hidden", entry.file.isHidden() ? "Yes" : "No");
    }

    /** Builds the preview of an entry. Runs on the loader thread (COM initialised). */
    PreviewPanel::Data load (const FileEntry& entry, juce::AudioFormatManager& formats)
    {
        using Content = PreviewPanel::Content;

        PreviewPanel::Data data;
        addBaseDetails (data.details, entry);

        const auto& file = entry.file;
        const auto category = FileCategories::of (entry);

        if (entry.isDirectory)
        {
            data.content = Content::text;
            data.text = listFolder (file);
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

        if (data.image.isNull() && category == FileCategory::audio)
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

        if (data.content == Content::none)
            data.content = data.image.isValid() ? Content::image : (data.text.isNotEmpty() ? Content::text : Content::none);

        return data;
    }
}

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

    show (nullptr);

    // Opening the audio output can take seconds: start in the background right away.
    audio.prepareDevice();
}

PreviewPanel::~PreviewPanel()
{
    loader.removeAllJobs (true, 5000);
}

void PreviewPanel::show (const FileEntry* entry)
{
    const int request = ++generation;
    loader.removeAllJobs (false, 0);    // Drop queued requests; a running one finishes and is ignored.

    audio.unload();

    if (entry == nullptr || ! entry->file.exists())
    {
        title.setText ("No selection", juce::dontSendNotification);
        display ({});
        return;
    }

    title.setText (entry->name() + "  (loading...)", juce::dontSendNotification);

    loader.addJob ([safeThis = juce::Component::SafePointer<PreviewPanel> (this), &formats = formats, request, item = *entry]
    {
        const ScopedComInit com;
        auto data = load (item, formats);

        juce::MessageManager::callAsync ([safeThis, request, name = item.name(), data = std::move (data)]() mutable
        {
            if (safeThis != nullptr && safeThis->generation == request)
            {
                safeThis->title.setText (name, juce::dontSendNotification);
                safeThis->display (std::move (data));
            }
        });
    });
}

void PreviewPanel::display (Data data)
{
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

    area.removeFromTop (8);
    metadata.setBounds (area);
}
