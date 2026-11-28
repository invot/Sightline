#pragma once

#include "Widgets.h"
#include "../PluginProcessor.h"

#include <functional>

namespace sl::ui
{

class SetupPanel : public juce::Component, private juce::Timer
{
public:
    SetupPanel (SightLineProcessor&, std::function<void()> onCameraChanged,
                std::function<void()> onTestNote);
    ~SetupPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void setCameraNames (const juce::StringArray& names, int current);
    void setCameraStatus (const juce::String& name, bool live);
    void setStandaloneInfo (bool standalone, const juce::String& midiOut);

private:
    void timerCallback() override;

    SightLineProcessor& proc;
    std::function<void()> cameraChanged, testNote;

    juce::ComboBox cameraCombo;
    TogglePill mirrorPill { "Mirror", pal::brand };
    TogglePill builtInPill { "Built-in sound", pal::pink };
    TogglePill pinchPill { "Pinch to play", pal::amber };

    SliderRow sensitivity { "Sensitivity", pal::brand, " %" };
    SliderRow dwell       { "Dwell", pal::amber, " ms" };
    SliderRow release     { "Release", pal::amber, " ms" };
    SliderRow velocity    { "Velocity", pal::brand, "" };
    SliderRow opacity     { "Opacity", pal::brand, " %" };
    SliderRow volume      { "Volume", pal::pink, " %" };

    LabelledCombo melodyChan { "Melody channel" };
    LabelledCombo drumChan { "Drum channel" };
    LabelledCombo aimCombo { "Aim with" };

    juce::TextButton testButton { "Send a test note" };

    juce::Rectangle<int> cameraCard, playCard, midiCard;
    juce::Rectangle<int> cameraStatusArea, cameraNoteArea, playNoteArea, midiNoteArea;
    juce::String cameraStatus { "NO CAMERA" }, midiStatus;
    bool cameraLive = false, standalone = false, standaloneKnown = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SetupPanel)
};

} // namespace sl::ui
