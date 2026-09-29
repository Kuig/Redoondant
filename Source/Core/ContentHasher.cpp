#include "ContentHasher.h"

namespace
{
    constexpr int chunkSize = 64 * 1024;

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

bool ContentHasher::filesEqual (const juce::File& a, const juce::File& b, const ScanContext& context)
{
    juce::FileInputStream inA (a), inB (b);
    return inA.openedOk() && inB.openedOk() && streamsEqual (inA, inB, context);
}
