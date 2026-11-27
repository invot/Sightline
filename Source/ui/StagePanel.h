#pragma once

#include "Widgets.h"
#include "../PluginProcessor.h"

namespace sl::ui
{

/** The main view: the camera, with the targets floating translucently over its
    sides so the hand is visible behind the notes it is playing. */
class StagePanel : public juce::Component
{
public:
    explicit StagePanel (SightLineProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Called from the editor's timer with the newest camera frame. */
    void setFrame (const juce::Image&);
    void setCameraStatus (const juce::String& name, bool live);
    void setOverlayText (const juce::String&);

    SegmentedControl overlaySwitch;

private:
    void drawPlaceholder (juce::Graphics&, juce::Rectangle<float>);
    void drawRails (juce::Graphics&, juce::Rectangle<float>);
    void drawHands (juce::Graphics&, juce::Rectangle<float>);

    juce::Rectangle<float> toStage (juce::Rectangle<float> area, const Rect& norm) const
    {
        return { area.getX() + norm.x * area.getWidth(),
                 area.getY() + norm.y * area.getHeight(),
                 norm.w * area.getWidth(), norm.h * area.getHeight() };
    }

    SightLineProcessor& proc;
    juce::Image frame;
    juce::String cameraName { "NO CAMERA" };
    bool cameraLive = false;
    juce::String overlayText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StagePanel)
};

} // namespace sl::ui
