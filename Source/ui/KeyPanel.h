#pragma once

#include "Widgets.h"
#include "../PluginProcessor.h"

namespace sl::ui
{

class KeyPanel : public juce::Component, private juce::Timer
{
public:
    explicit KeyPanel (SightLineProcessor&);
    ~KeyPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void drawPitchClassMap (juce::Graphics&, juce::Rectangle<int>);

    SightLineProcessor& proc;

    LabelledCombo keyCombo { "Key" };
    LabelledCombo scaleCombo { "Scale" };
    juce::ComboBox octaveCombo;

    juce::Rectangle<int> keyCard, railCard, chipArea, octaveCaption;

    /** Everything the page draws from, so it can repaint when any of it
        changes, wherever the change came from. */
    struct Shown
    {
        int overlay = -1, root = -1, scale = -1, octave = 99, drumChannel = -1;
        bool operator== (const Shown&) const = default;
    } shown;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeyPanel)
};

} // namespace sl::ui
