#include "ArchiveReaders.h"

namespace
{
    constexpr int blockSize = 512;

    // Field offsets in a ustar header block.
    constexpr int nameOffset = 0,     nameLength = 100;
    constexpr int sizeOffset = 124,   sizeLength = 12;
    constexpr int typeOffset = 156;
    constexpr int magicOffset = 257;
    constexpr int prefixOffset = 345, prefixLength = 155;

    juce::String cString (const char* text, int maxLength)
    {
        int length = 0;

        while (length < maxLength && text[length] != 0)
            ++length;

        return juce::String::fromUTF8 (text, length);
    }

    /** Sizes are octal text, or big-endian binary when the top bit of the first byte is set (GNU). */
    juce::int64 parseSize (const char* field)
    {
        juce::int64 value = 0;

        if ((field[0] & 0x80) != 0)
        {
            value = field[0] & 0x7f;

            for (int i = 1; i < sizeLength; ++i)
                value = (value << 8) | (juce::uint8) field[i];

            return value;
        }

        for (int i = 0; i < sizeLength; ++i)
            if (field[i] >= '0' && field[i] <= '7')
                value = value * 8 + (field[i] - '0');

        return value;
    }

    /** Extracts "path" from a pax extended header ("<length> path=<value>\n" records). */
    juce::String paxPath (const juce::String& records)
    {
        for (const auto& line : juce::StringArray::fromLines (records))
        {
            const auto record = line.fromFirstOccurrenceOf (" ", false, false);

            if (record.startsWith ("path="))
                return record.fromFirstOccurrenceOf ("=", false, false);
        }

        return {};
    }

    /** .tar archives (ustar, GNU long names, pax paths), optionally gzip-compressed. */
    class TarArchiveReader final : public ArchiveReader
    {
    public:
        TarArchiveReader (const juce::File& f, bool gz) : file (f), gzipped (gz)
        {
            if (auto in = createStream())
                parse (*in);
        }

        std::unique_ptr<juce::InputStream> openEntry (size_t index) override
        {
            const auto offset = dataOffsets[index];

            if (stream == nullptr || stream->getPosition() > offset)
                stream = createStream();

            if (stream == nullptr || ! stream->setPosition (offset))
                return nullptr;

            return std::make_unique<juce::SubregionStream> (stream.get(), offset, entries[index].size, false);
        }

    private:
        juce::File file;
        bool gzipped;
        std::vector<juce::int64> dataOffsets;
        std::unique_ptr<juce::InputStream> stream;

        std::unique_ptr<juce::InputStream> createStream() const
        {
            auto in = std::make_unique<juce::FileInputStream> (file);

            if (! in->openedOk())
                return nullptr;

            if (! gzipped)
                return in;

            return std::make_unique<juce::GZIPDecompressorInputStream> (in.release(), true,
                                                                        juce::GZIPDecompressorInputStream::gzipFormat);
        }

        void parse (juce::InputStream& in)
        {
            juce::String pendingName;   // Set by GNU long-name or pax headers, applies to the next entry.
            char header[blockSize];

            for (;;)
            {
                const auto headerPosition = in.getPosition();

                if (in.read (header, blockSize) != blockSize || header[0] == 0)
                    break;

                const auto size = parseSize (header + sizeOffset);
                const auto type = header[typeOffset];
                const auto dataPosition = headerPosition + blockSize;

                if (type == 'L')
                {
                    pendingName = readText (in, size);
                }
                else if (type == 'x')
                {
                    const auto path = paxPath (readText (in, size));

                    if (path.isNotEmpty())
                        pendingName = path;
                }
                else if (type != 'g')
                {
                    const auto name = pendingName.isNotEmpty() ? pendingName : headerName (header);
                    pendingName.clear();

                    const bool isRegularFile = type == '0' || type == 0 || type == '7';

                    if (isRegularFile && ! name.endsWithChar ('/'))
                    {
                        entries.push_back ({ name, size });
                        dataOffsets.push_back (dataPosition);
                    }
                }

                const auto paddedSize = (size + blockSize - 1) / blockSize * blockSize;

                if (! in.setPosition (dataPosition + paddedSize))
                    break;
            }
        }

        static juce::String readText (juce::InputStream& in, juce::int64 size)
        {
            juce::MemoryBlock block;
            in.readIntoMemoryBlock (block, (juce::ssize_t) size);
            return cString (static_cast<const char*> (block.getData()), (int) block.getSize());
        }

        static juce::String headerName (const char* header)
        {
            const auto name = cString (header + nameOffset, nameLength);
            const bool isUstar = std::memcmp (header + magicOffset, "ustar", 5) == 0;
            const auto prefix = isUstar ? cString (header + prefixOffset, prefixLength) : juce::String();

            return prefix.isEmpty() ? name : prefix + "/" + name;
        }
    };
}

std::unique_ptr<ArchiveReader> ArchiveReaders::createTarReader (const juce::File& file, bool gzipped)
{
    return std::make_unique<TarArchiveReader> (file, gzipped);
}
