#pragma once

#include <JuceHeader.h>

/** A user-editable setting of a criterion, described declaratively so the UI and
    persistence can be generated generically (see ParametersPanel).
*/
struct Parameter
{
    enum class Kind { toggle, number, text, choice };

    juce::String id;
    juce::String label;
    Kind kind = Kind::text;
    juce::var defaultValue;
    juce::var value;
    juce::String suffix;            ///< Unit shown after numbers, e.g. "MB".
    juce::StringArray choices;      ///< For Kind::choice; the value is the chosen index.
    int editorWidth = 60;           ///< Preferred width of the editing widget.

    static Parameter toggle (const juce::String& id, const juce::String& label, bool defaultValue)
    {
        return make (id, label, Kind::toggle, defaultValue, 0);
    }

    static Parameter number (const juce::String& id, const juce::String& label, double defaultValue, const juce::String& suffix = {})
    {
        auto p = make (id, label, Kind::number, defaultValue, 60);
        p.suffix = suffix;
        return p;
    }

    static Parameter text (const juce::String& id, const juce::String& label, const juce::String& defaultValue, int width = 380)
    {
        return make (id, label, Kind::text, defaultValue, width);
    }

    static Parameter choice (const juce::String& id, const juce::String& label, const juce::StringArray& choices, int defaultIndex = 0)
    {
        auto p = make (id, label, Kind::choice, defaultIndex, 110);
        p.choices = choices;
        return p;
    }

    /** Converts a stored string back into a value of the right type. */
    juce::var parse (const juce::String& stored) const
    {
        switch (kind)
        {
            case Kind::toggle: return stored.getIntValue() != 0 || stored.equalsIgnoreCase ("true");
            case Kind::number: return stored.getDoubleValue();
            case Kind::choice: return juce::jlimit (0, juce::jmax (0, choices.size() - 1), stored.getIntValue());
            case Kind::text:   break;
        }

        return stored;
    }

private:
    static Parameter make (const juce::String& id, const juce::String& label, Kind kind, const juce::var& defaultValue, int width)
    {
        Parameter p;
        p.id = id;
        p.label = label;
        p.kind = kind;
        p.defaultValue = p.value = defaultValue;
        p.editorWidth = width;
        return p;
    }
};

/** The parameters of a criterion, with typed accessors used by analyses. */
class ParameterSet
{
public:
    ParameterSet() = default;
    ParameterSet (std::initializer_list<Parameter> list) : items (list) {}

    std::vector<Parameter>& all() noexcept                  { return items; }
    const std::vector<Parameter>& all() const noexcept      { return items; }

    const juce::var& operator[] (const juce::String& id) const
    {
        for (const auto& p : items)
            if (p.id == id)
                return p.value;

        jassertfalse;   // Unknown parameter id
        static const juce::var none;
        return none;
    }

    bool         getBool   (const juce::String& id) const  { return (*this)[id]; }
    double       getNumber (const juce::String& id) const  { return (*this)[id]; }
    int          getChoice (const juce::String& id) const  { return (*this)[id]; }
    juce::String getText   (const juce::String& id) const  { return (*this)[id].toString(); }

    /** Splits a text parameter on ';' into trimmed, non-empty tokens. */
    juce::StringArray getList (const juce::String& id) const
    {
        auto tokens = juce::StringArray::fromTokens (getText (id), ";", {});
        tokens.trim();
        tokens.removeEmptyStrings();
        return tokens;
    }

private:
    std::vector<Parameter> items;
};
