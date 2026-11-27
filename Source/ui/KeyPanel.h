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

    LabelledCombo keyCombo { "Key", pal::purple };
    LabelledCombo scaleCombo { "Scale", pal::purple };
    juce::ComboBox octaveCombo;

    juce::Rectangle<int> keyCard, railCard, chipArea;
    int lastOctave = 99;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeyPanel)
};

} // namespace sl::ui
