#pragma once

#include <JuceHeader.h>

/** A view on the persistent settings file where every key is prefixed with a namespace
    (e.g. "duplicates.recursive"), so components can store their state without clashing.
*/
class SettingsScope
{
public:
    SettingsScope (juce::PropertiesFile& file, const juce::String& prefix = {})
        : properties (&file), prefix (prefix) {}

    SettingsScope child (const juce::String& name) const       { return { *properties, key (name) }; }

    juce::String get (const juce::String& name, const juce::String& fallback = {}) const
    {
        return properties->getValue (key (name), fallback);
    }

    bool   getBool   (const juce::String& name, bool fallback) const   { return properties->getBoolValue (key (name), fallback); }
    double getDouble (const juce::String& name, double fallback) const { return properties->getDoubleValue (key (name), fallback); }

    void set (const juce::String& name, const juce::var& value)        { properties->setValue (key (name), value); }
    void remove (const juce::String& name)                             { properties->removeValue (key (name)); }

private:
    juce::PropertiesFile* properties;
    juce::String prefix;

    juce::String key (const juce::String& name) const   { return prefix.isEmpty() ? name : prefix + "." + name; }
};

/** Owns the application's settings file (%APPDATA%\Redoondant\Redoondant.settings on Windows). */
class Settings
{
public:
    Settings()
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "Redoondant";
        options.folderName = "Redoondant";
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        options.millisecondsBeforeSaving = 500;
        properties.setStorageParameters (options);
    }

    SettingsScope root()        { return { *properties.getUserSettings() }; }
    void flush()                { properties.saveIfNeeded(); }

private:
    juce::ApplicationProperties properties;
};
