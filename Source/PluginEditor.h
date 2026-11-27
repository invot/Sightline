#pragma once

#include "PluginProcessor.h"
#include "ui/KeyPanel.h"
#include "ui/SetupPanel.h"
#include "ui/StagePanel.h"

#include <juce_video/juce_video.h>

namespace sl
{

class SightLineEditor : public juce::AudioProcessorEditor,
                        private juce::Timer,
                        private juce::CameraDevice::Listener
{
public:
    explicit SightLineEditor (SightLineProcessor&);
    ~SightLineEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Runs one frame through the tracker and refreshes the stage. Public so the
        smoke harness can drive it without a camera. */
    void feedFrame (const juce::Image&);

    ui::TabBar* getTabBarForTesting() noexcept { return &tabs; }
    ui::StagePanel* getStageForTesting() noexcept { return &stage; }

    /** Draws the live-camera chrome for offscreen snapshots, which have no
        hardware attached. */
    void setCameraLiveForTesting (bool live) { cameraLiveForTesting = live; repaint(); }

    /** True when the landmark model loaded (false: the basic tracker). */
    bool usesHandModelForTesting() const noexcept { return tracker.usesModel(); }

private:
    void timerCallback() override;
    void imageReceived (const juce::Image&) override;

    /** Loads ONNX Runtime and the hand models; falls back to the heuristic
        tracker (and says why on the stage) if that fails. */
    void loadHandModel();
    void openCamera (int index);
    void closeCamera();
    void refreshCameraList();
    void triggerTestNote();
    void paintLogo (juce::Graphics&, juce::Rectangle<float>);

    SightLineProcessor& proc;

    ui::SightLineLNF lnf;
    ui::TabBar tabs { juce::StringArray { "Play", "Key", "Setup" } };
    ui::StagePanel stage;
    ui::KeyPanel keyPage;
    ui::SetupPanel setupPage;

    juce::TextButton audioButton { "Audio / MIDI settings" };

    std::unique_ptr<juce::CameraDevice> camera;
    juce::CameraDevice::Listener* listenerRegistered = nullptr;
    juce::StringArray cameraNames;
    int cameraIndexOpened = -1;
    int cameraRetryTicks = 0;      // counts towards reopening a camera that failed
    int housekeepingTicks = 0;     // once-a-second status refresh

    juce::CriticalSection frameLock;
    juce::Image pendingFrame;
    juce::Image mirroredFrame;   // reused when the view is mirrored

    HandTracker tracker;
    std::vector<uint8_t> rgbScratch;   // tracker wants packed RGB; JUCE stores BGR
    int framesThisSecond = 0;
    double fpsWindowStart = 0.0;
    float fps = 0.0f;

    bool cameraDisabled = false;   // set by the smoke harness: no hardware in tests
    bool cameraLiveForTesting = false;
    int testNoteOffTicks = -1;
    int testNote = 60;
    int testChannel = 1;

    juce::Rectangle<int> headerArea, footerArea, contentArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SightLineEditor)
};

} // namespace sl
