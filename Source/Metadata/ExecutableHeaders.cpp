/*  Minimal reader of Windows PE headers (.exe, .dll, ...): architecture, subsystem, .NET, link time. */

#include "MetadataReader.h"

namespace
{
    const juce::StringArray peExtensions { "exe", "dll", "sys", "scr", "ocx", "cpl", "efi", "com" };

    juce::String machineName (juce::uint16 machine)
    {
        switch (machine)
        {
            case 0x014c: return "x86 (32-bit)";
            case 0x8664: return "x64 (64-bit)";
            case 0xaa64: return "ARM64";
            case 0x01c4: return "ARM (32-bit)";
            default:     return "Unknown (0x" + juce::String::toHexString ((int) machine) + ")";
        }
    }

    juce::String subsystemName (juce::uint16 subsystem)
    {
        switch (subsystem)
        {
            case 1:  return "Native / driver";
            case 2:  return "Windows GUI";
            case 3:  return "Console";
            case 10: case 11: case 12: case 13: return "EFI";
            default: return {};
        }
    }

    template <typename T>
    T readAt (const juce::MemoryBlock& data, size_t offset)
    {
        T value {};

        if (offset + sizeof (T) <= data.getSize())
            std::memcpy (&value, static_cast<const char*> (data.getData()) + offset, sizeof (T));

        return juce::ByteOrder::swapIfBigEndian (value);
    }
}

Metadata MetadataReader::readExecutableHeaders (const juce::File& file)
{
    Metadata metadata;

    if (! peExtensions.contains (file.getFileExtension().fromFirstOccurrenceOf (".", false, false), true))
        return metadata;

    juce::FileInputStream in (file);
    juce::MemoryBlock data;

    if (! in.openedOk() || in.readIntoMemoryBlock (data, 4096) < 64 || readAt<juce::uint16> (data, 0) != 0x5a4d)   // "MZ"
        return metadata;

    const auto pe = (size_t) readAt<juce::uint32> (data, 0x3c);

    if (readAt<juce::uint32> (data, pe) != 0x00004550)     // "PE\0\0"
        return metadata;

    const auto coff = pe + 4;
    const auto optional = coff + 20;
    const auto characteristics = readAt<juce::uint16> (data, coff + 18);
    const bool is64 = readAt<juce::uint16> (data, optional) == 0x20b;
    const auto dataDirectories = optional + (is64 ? 112 : 96);
    const bool isDotNet = readAt<juce::uint32> (data, dataDirectories + 14 * 8) != 0;   // CLR runtime header

    metadata.add ("Pe.Machine", "Architecture", machineName (readAt<juce::uint16> (data, coff)));
    metadata.add ("Pe.Kind", "Binary type", juce::String ((characteristics & 0x2000) != 0 ? "Library (DLL)" : "Executable")
                                              + (isDotNet ? ", .NET" : ""));
    metadata.add ("Pe.Subsystem", "Subsystem", subsystemName (readAt<juce::uint16> (data, optional + 68)));

    // Reproducible builds store a hash here instead of a time: only show plausible dates.
    const auto linkTime = juce::Time ((juce::int64) readAt<juce::uint32> (data, coff + 4) * 1000);

    if (linkTime.getYear() >= 1995 && linkTime < juce::Time::getCurrentTime())
        metadata.add ("Pe.LinkTime", "Linked", linkTime.formatted ("%Y-%m-%d %H:%M"));

    return metadata;
}
