#pragma once

#include <JuceHeader.h>

namespace ShellContextMenu
{
    /** Shows the operating system's context menu (the Explorer right-click menu) for files
        of the same folder, at a screen position, and runs the chosen command.
        Returns true if a command was chosen. Does nothing (returns false) on other platforms.
    */
    bool show (const juce::Array<juce::File>& files, juce::Point<int> screenPosition, juce::Component& owner);
}
