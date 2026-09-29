#include "PreviewPanel.h"
#include "../Core/FileCategory.h"
#include "../Core/Format.h"

namespace
{
    constexpr juce::int64 maxImageBytes = 64 * 1024 * 1024;
    constexpr int maxTextBytes = 64 * 1024;
    constexpr int maxFolderItems = 500;

    void setUpReadOnly (juce::TextEditor& editor, bool monospaced)
    {
        editor.setMultiLine (true);
        editor.setReadOnly (true);
        editor.setScrollbarsShown (true);
        editor.setCaretVisible (false);

        if (monospaced)
            editor.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
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

    juce::String listFolder (const juce::File& folder)
    {
        juce::StringArray names;

        for (const auto& item : juce::RangedDirectoryIterator (folder, false, "*", juce::File::findFilesAndDirectories))
        {
            if (names.size() >= maxFolderItems)
            {
                names.add ("...");
                break;
            }

            names.add (item.getFile().getFileName() + (item.isDirectory() ? "/" : ""));
        }

        names.sortNatural();
        return names.joinIntoString ("\n");
    }
}

PreviewPanel::PreviewPanel (SettingsScope settings)
    : audio (formats, settings.child ("audio"))
{
    formats.registerBasicFormats();

    title.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    title.setMinimumHorizontalScale (0.6f);
    image.setImagePlacement (juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize);
    setUpReadOnly (text, true);
    setUpReadOnly (metadata, false);

    addAndMakeVisible (title);
    addChildComponent (image);
    addChildComponent (text);
    addChildComponent (audio);
    addAndMakeVisible (metadata);

    show (nullptr);
}

PreviewPanel::~PreviewPanel() = default;

void PreviewPanel::show (const FileEntry* entry)
{
    audio.unload();
    image.setImage ({});
    text.clear();

    juce::StringPairArray details;

    if (entry == nullptr || ! entry->file.exists())
    {
        title.setText ("No selection", juce::dontSendNotification);
        content = Content::none;
    }
    else
    {
        title.setText (entry->name(), juce::dontSendNotification);
        details.set ("Path", entry->file.getFullPathName());
        details.set ("Type", entry->isDirectory ? juce::String ("Folder") : FileCategories::nameOf (FileCategories::of (*entry)));
        details.set ("Size", Format::size (entry->size) + " (" + juce::String (entry->size) + " bytes)");

        if (entry->isDirectory)
            details.set ("Files", juce::String (entry->fileCount));

        details.set ("Created", Format::date (entry->created));
        details.set ("Modified", Format::date (entry->modified));
        details.set ("Accessed", Format::date (entry->file.getLastAccessTime()));
        details.set ("Read-only", entry->file.hasWriteAccess() ? "No" : "Yes");
        details.set ("Hidden", entry->file.isHidden() ? "Yes" : "No");

        content = loadContent (*entry, details);
    }

    image.setVisible (content == Content::image);
    text.setVisible (content == Content::text);
    audio.setVisible (content == Content::audio);
    showDetails (details);
    resized();
}

PreviewPanel::Content PreviewPanel::loadContent (const FileEntry& entry, juce::StringPairArray& details)
{
    const auto category = FileCategories::of (entry);

    if (entry.isDirectory)
    {
        text.setText (listFolder (entry.file), false);
        return Content::text;
    }

    if (category == FileCategory::image && entry.size <= maxImageBytes)
    {
        const auto loaded = juce::ImageFileFormat::loadFrom (entry.file);

        if (loaded.isValid())
        {
            image.setImage (loaded);
            details.set ("Dimensions", juce::String (loaded.getWidth()) + " x " + juce::String (loaded.getHeight()) + " px");
            return Content::image;
        }
    }

    if (category == FileCategory::audio || category == FileCategory::video)
    {
        if (audio.load (entry.file))
        {
            details.addArray (audio.getDetails());
            return Content::audio;
        }
    }

    if (looksLikeText (entry.file))
    {
        text.setText (readTextStart (entry.file), false);
        return Content::text;
    }

    return Content::none;
}

void PreviewPanel::showDetails (const juce::StringPairArray& details)
{
    juce::String lines;

    for (int i = 0; i < details.size(); ++i)
        lines << details.getAllKeys()[i] << ":  " << details.getAllValues()[i] << "\n";

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

    auto preview = area.removeFromTop (content == Content::audio ? 86 : (content == Content::none ? 0 : area.getHeight() * 55 / 100));

    for (auto* c : std::initializer_list<juce::Component*> { &image, &text, &audio })
        c->setBounds (preview);

    area.removeFromTop (8);
    metadata.setBounds (area);
}
