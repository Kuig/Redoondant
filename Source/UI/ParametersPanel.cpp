#include "ParametersPanel.h"

namespace
{
    constexpr int lineHeight = 24;
    constexpr int lineGap = 6;
    constexpr int itemGap = 16;
    constexpr int innerGap = 6;
    constexpr int toggleWidth = 26;

    /** "500" rather than "500.0"; up to 3 decimals otherwise. */
    juce::String formatNumber (double value)
    {
        if (value == std::floor (value))
            return juce::String ((juce::int64) value);

        return juce::String (value, 3).trimCharactersAtEnd ("0");
    }

    int textWidth (const juce::String& text)
    {
        return text.isEmpty() ? 0 : juce::GlyphArrangement::getStringWidthInt (juce::Font (juce::FontOptions (14.0f)), text) + 8;
    }
}

/** One parameter's widgets: an optional label, the editing widget and an optional unit. */
struct ParametersPanel::Editor
{
    juce::Label label, suffix;
    std::unique_ptr<juce::ToggleButton> enableToggle;     ///< Only for parameters with hasToggle.
    std::unique_ptr<juce::Component> widget;
    int widgetWidth = 0;
    std::function<void()> refresh;      ///< Updates the widget from the parameter value.

    int getWidth() const
    {
        return (enableToggle != nullptr ? toggleWidth : 0) + textWidth (label.getText()) + widgetWidth + (suffix.getText().isEmpty() ? 0 : innerGap + textWidth (suffix.getText()));
    }
};

ParametersPanel::ParametersPanel (ParameterSet& p, SettingsScope s)
    : parameters (p), settings (s)
{
    for (auto& parameter : parameters.all())
    {
        parameter.value = parameter.parse (settings.get (parameter.id, parameter.defaultValue.toString()));
        parameter.enabled = settings.getBool (parameter.id + ".on", parameter.defaultEnabled);
        editors.push_back (createEditor (parameter));
    }
}

ParametersPanel::~ParametersPanel() = default;

std::unique_ptr<ParametersPanel::Editor> ParametersPanel::createEditor (Parameter& parameter)
{
    auto editor = std::make_unique<Editor>();
    editor->widgetWidth = parameter.editorWidth;
    editor->suffix.setText (parameter.suffix, juce::dontSendNotification);

    switch (parameter.kind)
    {
        case Parameter::Kind::toggle:
        {
            auto toggle = std::make_unique<juce::ToggleButton> (parameter.label);
            toggle->onClick = [this, &parameter, t = toggle.get()] { changed (parameter, t->getToggleState()); };
            editor->refresh = [&parameter, t = toggle.get()] { t->setToggleState ((bool) parameter.value, juce::dontSendNotification); };
            editor->widgetWidth = textWidth (parameter.label) + 30;
            editor->widget = std::move (toggle);
            break;
        }

        case Parameter::Kind::number:
        case Parameter::Kind::text:
        {
            const bool isNumber = parameter.kind == Parameter::Kind::number;
            auto text = std::make_unique<juce::TextEditor>();

            if (isNumber)
            {
                text->setInputRestrictions (12, "0123456789.");
                text->setJustification (juce::Justification::centredRight);
            }

            text->onTextChange = [this, &parameter, isNumber, t = text.get()]
            {
                changed (parameter, isNumber ? juce::var (t->getText().getDoubleValue()) : juce::var (t->getText()));
            };
            editor->refresh = [&parameter, isNumber, t = text.get()]
            {
                t->setText (isNumber ? formatNumber (parameter.value) : parameter.value.toString(), false);
            };
            editor->label.setText (parameter.label, juce::dontSendNotification);
            editor->widget = std::move (text);
            break;
        }

        case Parameter::Kind::choice:
        {
            auto combo = std::make_unique<juce::ComboBox>();
            combo->addItemList (parameter.choices, 1);
            combo->onChange = [this, &parameter, c = combo.get()] { changed (parameter, c->getSelectedItemIndex()); };
            editor->refresh = [&parameter, c = combo.get()] { c->setSelectedItemIndex ((int) parameter.value, juce::dontSendNotification); };
            editor->label.setText (parameter.label, juce::dontSendNotification);
            editor->widget = std::move (combo);
            break;
        }
    }

    if (parameter.hasToggle)
    {
        editor->enableToggle = std::make_unique<juce::ToggleButton>();
        editor->enableToggle->onClick = [this, &parameter, e = editor.get()]
        {
            parameter.enabled = e->enableToggle->getToggleState();
            settings.set (parameter.id + ".on", parameter.enabled);
            e->widget->setEnabled (parameter.enabled);

            if (onChange != nullptr)
                onChange();
        };

        auto refreshValue = std::move (editor->refresh);
        editor->refresh = [&parameter, e = editor.get(), refreshValue]
        {
            refreshValue();
            e->enableToggle->setToggleState (parameter.enabled, juce::dontSendNotification);
            e->widget->setEnabled (parameter.enabled);
        };
        addAndMakeVisible (*editor->enableToggle);
    }

    editor->refresh();
    addAndMakeVisible (editor->label);
    addAndMakeVisible (*editor->widget);
    addAndMakeVisible (editor->suffix);
    return editor;
}

void ParametersPanel::changed (Parameter& parameter, const juce::var& newValue)
{
    parameter.value = newValue;
    settings.set (parameter.id, newValue);

    if (onChange != nullptr)
        onChange();
}

void ParametersPanel::resetToDefaults()
{
    for (size_t i = 0; i < editors.size(); ++i)
    {
        auto& parameter = parameters.all()[i];
        parameter.value = parameter.defaultValue;
        parameter.enabled = parameter.defaultEnabled;
        settings.remove (parameter.id);
        settings.remove (parameter.id + ".on");
        editors[i]->refresh();
    }

    if (onChange != nullptr)
        onChange();
}

int ParametersPanel::getHeightForWidth (int width) const
{
    return layout (width, false);
}

void ParametersPanel::resized()
{
    layout (getWidth(), true);
}

int ParametersPanel::layout (int width, bool apply) const
{
    if (editors.empty())
        return 0;

    int x = 0, y = 0;

    for (const auto& editor : editors)
    {
        const int w = editor->getWidth();

        if (x > 0 && x + w > width)
        {
            x = 0;
            y += lineHeight + lineGap;
        }

        if (apply)
        {
            juce::Rectangle<int> line (x, y, w, lineHeight);

            if (editor->enableToggle != nullptr)
                editor->enableToggle->setBounds (line.removeFromLeft (toggleWidth));

            editor->label.setBounds (line.removeFromLeft (textWidth (editor->label.getText())));
            editor->widget->setBounds (line.removeFromLeft (editor->widgetWidth));
            editor->suffix.setBounds (line.withTrimmedLeft (innerGap));
        }

        x += w + itemGap;
    }

    return y + lineHeight;
}
