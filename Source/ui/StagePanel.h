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
    void mouseDown (const juce::MouseEvent&) override;

    /** Called from the editor's timer with the newest camera frame. */
    void setFrame (const juce::Image&);
    void setCameraStatus (const juce::String& name, bool live);
    void setOverlayText (const juce::String&);

    /** The tracker's debug mask for the newest frame: 0 empty, 90 motion,
        170 skin, 255 both. Kept while the view is off, so switching it on
        shows the last analysed frame immediately. */
    void setTrackingMask (const std::vector<uint8_t>& values, int width, int height);

    /** The landmark model's 21 points per hand, in 0..1 stage coordinates,
        index 0 = left rail. Drawn while the tracking test is on. */
    struct Skeleton
    {
        bool valid = false;
        bool pinching = false;   // the trigger (pinch or thumb click) is closed
        bool pointing = false;
        juce::Point<float> points[HandPose::numPoints];
    };

    void setSkeletons (const Skeleton (&hands)[2]);

    /** Which tracker is running, for the tracking test readout. */
    void setTrackerInfo (const juce::String& text, bool usingModel);

    /** Draws the mask, the two search regions and the hand/fps readout over the
        camera picture. */
    void setTrackingDebug (bool shouldShow);
    bool isTrackingDebug() const noexcept { return trackingDebug; }
    bool hasTrackingMask() const noexcept { return maskW > 0 && ! mask.empty(); }

    /** Follows the overlay parameter when it changes from outside the switch
        (automation, undo, preset load). */
    void syncOverlay();

    /** The picture fills the stage without being stretched, so part of the
        frame is cropped. Converts a hand position from frame coordinates
        (0..1) to stage coordinates, which is where the targets live. */
    HandSample frameToStage (HandSample, int frameWidth, int frameHeight) const;

    SegmentedControl overlaySwitch;

private:
    void drawPlaceholder (juce::Graphics&, juce::Rectangle<float>);
    void drawRails (juce::Graphics&, juce::Rectangle<float>);
    void drawHands (juce::Graphics&, juce::Rectangle<float>);
    void rebuildMaskImage();
    void drawTrackingOverlay (juce::Graphics&, juce::Rectangle<float>);
    void drawSkeletons (juce::Graphics&, juce::Rectangle<float>);
    void drawTrackingReadout (juce::Graphics&, juce::Rectangle<float>);
    void drawLearnBanner (juce::Graphics&, juce::Rectangle<float>, int pad);
    void showPadMenu (int pad);

    /** The part of a frame that is visible on the stage, in 0..1 frame units. */
    Rect visibleFrameArea (int frameWidth, int frameHeight) const;

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
    int shownOverlay = -1;

    TogglePill trackingPill { "Tracking test", pal::yellow };
    bool trackingDebug = false;
    std::vector<uint8_t> mask;
    int maskW = 0, maskH = 0;
    juce::Image maskImage;
    Skeleton skeletons[2];
    juce::String trackerInfo;
    bool trackerIsModel = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StagePanel)
};

} // namespace sl::ui
