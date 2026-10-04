#include "ContentHasher.h"

namespace
{
    constexpr int chunkSize = 64 * 1024;
    constexpr int hashBufferSize = 1024 * 1024;     // SHA256 reads 64 bytes at a time: from memory, not from the disk.

    /** Stream that stops (reads nothing more) as soon as the scan is cancelled, so long hashes can be interrupted;
        it counts the bytes it delivered.
    */
    class CancellableStream final : public juce::InputStream
    {
    public:
        CancellableStream (juce::InputStream& s, const ScanContext& c) : source (s), context (c) {}

        juce::int64 getTotalLength() override       { return source.getTotalLength(); }
        bool isExhausted() override                 { return cancelled || source.isExhausted(); }
        juce::int64 getPosition() override          { return source.getPosition(); }
        bool setPosition (juce::int64 p) override   { return source.setPosition (p); }

        int read (void* buffer, int bytes) override
        {
            if (context.isCancelled())
                cancelled = true;

            const int read = cancelled ? 0 : source.read (buffer, bytes);
            delivered += juce::jmax (0, read);
            return read;
        }

        bool wasCancelled() const noexcept          { return cancelled; }
        juce::int64 bytesDelivered() const noexcept { return delivered; }

    private:
        juce::InputStream& source;
        const ScanContext& context;
        bool cancelled = false;
        juce::int64 delivered = 0;
    };

    juce::uint64 fnv1a (const void* data, size_t size, juce::uint64 hash = 0xcbf29ce484222325ull)
    {
        const auto* bytes = static_cast<const juce::uint8*> (data);

        for (size_t i = 0; i < size; ++i)
            hash = (hash ^ bytes[i]) * 0x100000001b3ull;

        return hash;
    }
}

juce::uint64 ContentHasher::quickHash (const juce::File& file)
{
    juce::FileInputStream in (file);

    if (! in.openedOk())
        return 0;

    juce::MemoryBlock sample;
    in.readIntoMemoryBlock (sample, chunkSize);

    if (in.getTotalLength() > 2 * chunkSize)
    {
        in.setPosition (in.getTotalLength() - chunkSize);
        in.readIntoMemoryBlock (sample, chunkSize);
    }
    else
    {
        in.readIntoMemoryBlock (sample);
    }

    return fnv1a (sample.getData(), sample.getSize());
}

std::optional<juce::uint64> ContentHasher::startHash (const juce::File& file)
{
    juce::FileInputStream in (file);

    if (! in.openedOk())
        return std::nullopt;

    juce::HeapBlock<char> buffer (chunkSize);
    const int read = in.read (buffer, chunkSize);

    if (read < 0 || in.getStatus().failed())
        return std::nullopt;

    return fnv1a (buffer, (size_t) read, fnv1a (&read, sizeof (read)));
}

juce::String ContentHasher::checksum (const juce::File& file, const ScanContext& context, juce::int64* bytesRead)
{
    juce::FileInputStream in (file);

    if (! in.openedOk())
        return {};

    CancellableStream cancellable (in, context);
    juce::BufferedInputStream buffered (cancellable, hashBufferSize);
    const juce::SHA256 hash (buffered);

    if (bytesRead != nullptr)
        *bytesRead = cancellable.bytesDelivered();

    // A read error ends the stream early: that would be the checksum of a part of the file.
    return cancellable.wasCancelled() || in.getStatus().failed() ? juce::String() : hash.toHexString();
}

bool ContentHasher::streamsEqual (juce::InputStream& a, juce::InputStream& b, const ScanContext& context)
{
    juce::HeapBlock<char> bufferA (chunkSize), bufferB (chunkSize);

    for (;;)
    {
        if (context.isCancelled())
            return false;

        const int readA = a.read (bufferA, chunkSize);
        const int readB = b.read (bufferB, chunkSize);

        if (readA != readB || (readA > 0 && std::memcmp (bufferA, bufferB, (size_t) readA) != 0))
            return false;

        if (readA <= 0)
            return true;
    }
}
