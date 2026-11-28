#include "PluginEditor.h"
#include "ui/Logo.h"
#include "StandaloneBridge.h"
#include "core/OrtModel.h"

#if SL_HAVE_ORT
 #include "SightLineModels.h"
#elif SL_HAVE_VISION
 #include "platform/VisionHandPose.h"
#endif

namespace sl
{

namespace
{
    /** Flips a frame horizontally into a reused buffer. The picture is mirrored
        in pixels rather than with a Graphics transform: drawing an image under
        a transform proved unreliable in the offscreen renderer, and this keeps
        the picture, the debug mask and the hand markers in the same space. */
    juce::Image mirrorHorizontally (const juce::Image& source, juce::Image& scratch)
    {
        const int w = source.getWidth();
        const int h = source.getHeight();

        if (! scratch.isValid() || scratch.getWidth() != w || scratch.getHeight() != h)
            scratch = juce::Image (juce::Image::RGB, w, h, false, juce::SoftwareImageType());

        const juce::Image::BitmapData in (source, juce::Image::BitmapData::readOnly);
        juce::Image::BitmapData out (scratch, juce::Image::BitmapData::writeOnly);

        for (int y = 0; y < h; ++y)
        {
            const auto* src = in.getLinePointer (y);
            auto* dst = out.getLinePointer (y);

            for (int x = 0; x < w; ++x)
            {
                const auto* s = src + (size_t) x * (size_t) in.pixelStride;
                auto* d = dst + (size_t) (w - 1 - x) * (size_t) out.pixelStride;
                d[0] = s[0];
                d[1] = s[1];
                d[2] = s[2];

                if (out.pixelStride > 3)
                    d[3] = 0xff;
            }
        }

        return scratch;
    }
}

SightLineEditor::SightLineEditor (SightLineProcessor& p)
    : AudioProcessorEditor (&p),
      proc (p),
      stage (p),
      keyPage (p),
      setupPage (p, [this] { openCamera (proc.cameraIndex()); },
                    [this] { triggerTestNote(); })
{
    setLookAndFeel (&lnf);

    addAndMakeVisible (tabs);
    addAndMakeVisible (stage);
    addChildComponent (keyPage);
    addChildComponent (setupPage);
    addAndMakeVisible (audioButton);

    audioButton.setColour (juce::TextButton::buttonColourId, ui::pal::panelHi);
    audioButton.setTooltip ("Choose the audio device and the MIDI output");
    audioButton.onClick = [] { standalone::showAudioSettings(); };
    audioButton.setVisible (standalone::isRunning());

    tabs.onTabChanged = [this] (int index)
    {
        stage.setVisible (index == 0);
        keyPage.setVisible (index == 1);
        setupPage.setVisible (index == 2);

        if (index == 2)
            refreshCameraList();
    };

    keyPage.setVisible (false);
    setupPage.setVisible (false);

    setResizable (true, true);
    setResizeLimits (1040, 700, 1600, 1000);
    setSize (1180, 780);

    cameraDisabled = juce::SystemStats::getEnvironmentVariable ("SIGHTLINE_NO_CAMERA", {}).isNotEmpty();
    loadHandModel();

    // TEMP DIAG
    if (const auto forced = juce::SystemStats::getEnvironmentVariable ("SIGHTLINE_CAMERA_INDEX", {});
        forced.isNotEmpty())
        if (auto* param = proc.apvts.getParameter ("cameraIndex"))
            param->setValueNotifyingHost (param->convertTo0to1 ((float) forced.getIntValue()));

    refreshCameraList();

    if (cameraDisabled)
    {
        stage.setCameraStatus ("no camera", false);
        setupPage.setCameraStatus ("disabled in tests", false);
    }
    else
    {
        openCamera (proc.cameraIndex());
    }

    standalone::unmuteInput();
    standalone::syncMidiOutput();
    setupPage.setStandaloneInfo (proc.isStandaloneApp(), standalone::midiOutputName());

    startTimerHz (60);
}

SightLineEditor::~SightLineEditor()
{
    stopTimer();
    proc.cancelDrumLearn();
    closeCamera();
    proc.stopTracking();
    setLookAndFeel (nullptr);
}

//==============================================================================
void SightLineEditor::loadHandModel()
{
    std::string error;

   #if SL_HAVE_ORT
    // onnxruntime.dll ships next to the app, and inside the VST3 bundle next to
    // the plugin binary; currentExecutableFile is the plugin's own module.
    const auto dll = juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                         .getSiblingFile ("onnxruntime.dll");

    if (ort::loadRuntime (dll.getFullPathName().toWideCharPointer(), error))
    {
        auto palm = ort::createModel (SightLineModels::palm_detection_onnx,
                                      (size_t) SightLineModels::palm_detection_onnxSize,
                                      { 1, 192, 192, 3 }, error);
        auto hand = palm == nullptr ? nullptr
                  : ort::createModel (SightLineModels::hand_landmark_onnx,
                                      (size_t) SightLineModels::hand_landmark_onnxSize,
                                      { 1, 224, 224, 3 }, error);

        if (palm != nullptr && hand != nullptr)
        {
            tracker.setLandmarker (std::make_unique<HandLandmarker> (std::move (palm), std::move (hand)));
            stage.setTrackerInfo ("Hand model: pinch to play", true);
            return;
        }
    }
#elif SL_HAVE_VISION
    if (auto landmarker = makeVisionHandLandmarker())
    {
        tracker.setLandmarker (std::move (landmarker));
        stage.setTrackerInfo ("Hand model: pinch to play", true);
        return;
    }
    error = "Apple Vision hand tracking unavailable";
#else
    error = "built without a hand landmark model";
   #endif

    DBG ("sightline: hand model unavailable: " << error);
    stage.setTrackerInfo ("Basic tracker (" + juce::String (error) + ")", false);
}

void SightLineEditor::refreshCameraList()
{
    cameraNames = juce::CameraDevice::getAvailableDevices();
    setupPage.setCameraNames (cameraNames, proc.cameraIndex());

    // TEMP DIAG
    std::fprintf (stderr, "[diag] cameras: %d\n", cameraNames.size());
    for (int i = 0; i < cameraNames.size(); ++i)
        std::fprintf (stderr, "[diag]   %d: %s\n", i, cameraNames[i].toRawUTF8());
}

void SightLineEditor::openCamera (int index)
{
    closeCamera();
    cameraRetryTicks = 0;

    if (cameraNames.isEmpty())
        refreshCameraList();

    if (cameraNames.isEmpty())
    {
        cameraIndexOpened = -2;
        stage.setCameraStatus ("no camera", false);
        setupPage.setCameraStatus ("no camera", false);
        return;
    }

    index = juce::jlimit (0, cameraNames.size() - 1, index);
    camera.reset (juce::CameraDevice::openDevice (index, 640, 480, 1920, 1080, true));

    if (camera == nullptr)
    {
        cameraIndexOpened = -2;
        stage.setCameraStatus ("in use?", false);
        setupPage.setCameraStatus ("could not open", false);
        return;
    }

    camera->addListener (this);
    listenerRegistered = this;
    cameraIndexOpened = index;

    // TEMP DIAG
    std::fprintf (stderr, "[diag] opened %d: %s\n", index, camera->getName().toRawUTF8());
    tracker.reset();

    stage.setCameraStatus (camera->getName(), true);
    setupPage.setCameraStatus (camera->getName(), true);
}

void SightLineEditor::closeCamera()
{
    if (camera != nullptr)
    {
        if (listenerRegistered != nullptr)
            camera->removeListener (listenerRegistered);

        camera.reset();
    }

    listenerRegistered = nullptr;
    cameraIndexOpened = -1;

    const juce::ScopedLock lock (frameLock);
    pendingFrame = {};
}

void SightLineEditor::imageReceived (const juce::Image& image)
{
    const juce::ScopedLock lock (frameLock);
    pendingFrame = image;
}

//==============================================================================
void SightLineEditor::feedFrame (const juce::Image& image)
{
    if (! image.isValid())
        return;

    // TEMP DIAG
    const auto diagStart = juce::Time::getMillisecondCounterHiRes();

    juce::Image source = image;

    if (source.getFormat() != juce::Image::RGB)
        source = source.convertedToFormat (juce::Image::RGB);

    const juce::Image::BitmapData data (source, juce::Image::BitmapData::readOnly);
    const int w = source.getWidth();
    const int h = source.getHeight();

    // JUCE keeps its pixels in BGR(A) order, while the detector works on packed
    // RGB, so repack once per frame into a reused buffer. The pixel stride is
    // read from the bitmap: native (Direct2D) RGB images store 4 bytes a pixel.
    rgbScratch.resize ((size_t) w * (size_t) h * 3u);
    const auto stride = (size_t) data.pixelStride;

    for (int y = 0; y < h; ++y)
    {
        const auto* row = data.getLinePointer (y);
        auto* dst = rgbScratch.data() + (size_t) y * (size_t) w * 3u;

        for (int x = 0; x < w; ++x)
        {
            const auto* px = row + (size_t) x * stride;
            dst[x * 3]     = px[2];
            dst[x * 3 + 1] = px[1];
            dst[x * 3 + 2] = px[0];
        }
    }

    const auto diagRepack = juce::Time::getMillisecondCounterHiRes();

    const double now = juce::Time::getMillisecondCounterHiRes();
    tracker.setAimMode (proc.aimMode());
    tracker.analyse (rgbScratch.data(), w, h, w * 3, proc.sensitivity(), now);
    stage.setTrackingMask (tracker.lastMask(), tracker.maskWidth(), tracker.maskHeight());

    const auto diagTrack = juce::Time::getMillisecondCounterHiRes();

    auto left  = tracker.side (0);
    auto right = tracker.side (1);

    // The skeletons for the tracking test, in stage coordinates.
    ui::StagePanel::Skeleton skeletons[2];

    for (int s = 0; s < 2; ++s)
    {
        const auto* pose = tracker.pose (s);

        if (pose == nullptr)
            continue;

        const int shown = proc.isMirrored() ? 1 - s : s;
        auto& sk = skeletons[shown];
        sk.valid = true;
        sk.pinching = tracker.aimMode() == AimMode::Fingertip ? pose->clicking : pose->pinching;
        sk.pointing = pose->pointing;

        for (int i = 0; i < HandPose::numPoints; ++i)
        {
            HandSample pt;
            pt.x = proc.isMirrored() ? 1.0f - pose->x[i] : pose->x[i];
            pt.y = pose->y[i];
            pt = stage.frameToStage (pt, w, h);
            sk.points[i] = { pt.x, pt.y };
        }
    }

    stage.setSkeletons (skeletons);

    if (proc.isMirrored())
    {
        // The view is mirrored, so a hand the camera sees on its left is drawn
        // on the right: mirror the position and swap the sides, otherwise the
        // targets (which live in display coordinates) would be off by a rail.
        left.x  = 1.0f - left.x;
        right.x = 1.0f - right.x;
        std::swap (left, right);
    }

    // The stage crops the picture to fill without stretching it.
    left  = stage.frameToStage (left, w, h);
    right = stage.frameToStage (right, w, h);

    proc.updateHands (left, right, now);

    stage.setFrame (proc.isMirrored() ? mirrorHorizontally (source, mirroredFrame) : source);
    ++framesThisSecond;

    // TEMP DIAG
    {
        const auto diagEnd = juce::Time::getMillisecondCounterHiRes();
        static int diagFrames = 0;

        if (++diagFrames % 30 == 0)
            std::fprintf (stderr, "[diag] %dx%d repack %.2f track %.2f rest %.2f total %.2f\n",
                          w, h, diagRepack - diagStart, diagTrack - diagRepack,
                          diagEnd - diagTrack, diagEnd - diagStart);
    }
}

void SightLineEditor::timerCallback()
{
    proc.syncZones();

    // ---- apply a learned drum note on the message thread -----------------
    int learnedPad = -1, learnedNote = -1;

    if (proc.takeLearnedNote (learnedPad, learnedNote))
    {
        proc.setDrumNote (learnedPad, learnedNote);
        stage.repaint();
    }

    // ---- follow the camera choice ----------------------------------------
    const int wanted = proc.cameraIndex();

    if (! cameraDisabled && wanted != cameraIndexOpened)
    {
        if (cameraIndexOpened == -2)
        {
            if (++cameraRetryTicks >= 60)      // every two seconds, retry a failed open
            {
                cameraRetryTicks = 0;
                refreshCameraList();
                openCamera (wanted);
            }
        }
        else
        {
            openCamera (wanted);
        }
    }

    // ---- newest camera frame ---------------------------------------------
    juce::Image frame;

    {
        const juce::ScopedLock lock (frameLock);
        frame = pendingFrame;
        pendingFrame = {};
    }

    if (frame.isValid())
        feedFrame (frame);

    // ---- fps -------------------------------------------------------------
    const double now = juce::Time::getMillisecondCounterHiRes();

    if (now - fpsWindowStart >= 1000.0)
    {
        fps = (float) framesThisSecond;
        framesThisSecond = 0;
        fpsWindowStart = now;
        proc.setCameraFps (fps);

        // TEMP DIAG
        std::fprintf (stderr, "[diag] fps %.1f\n", fps);
    }

    // ---- housekeeping ----------------------------------------------------
    stage.syncOverlay();

    if (testNoteOffTicks > 0 && --testNoteOffTicks == 0)
        proc.playPreviewNote (testNote, testChannel, -1);

    if (++housekeepingTicks >= 30)
    {
        housekeepingTicks = 0;
        standalone::unmuteInput();
        standalone::syncMidiOutput();
        setupPage.setStandaloneInfo (proc.isStandaloneApp(), standalone::midiOutputName());
        setupPage.setCameraStatus (cameraIndexOpened >= 0 ? cameraNames[cameraIndexOpened] : juce::String(),
                                   cameraIndexOpened >= 0);
    }

    if (const int pad = proc.learningPad(); pad >= 0)
        stage.setOverlayText (juce::String ("MIDI learn: ") + drumPadName (pad)
                              + " - play a note on your controller, or right-click the pad to cancel.");
    else
    {
        // How to play depends on the tracker and the Playing settings.
        juce::String how;

        if (! tracker.usesModel() || ! proc.pinchToPlay())
            how = proc.aimMode() == AimMode::Fingertip && tracker.usesModel()
                    ? "point at a target and hold still to play it"
                    : "hold a hand on a target to play it";
        else if (proc.aimMode() == AimMode::Fingertip)
            how = "point at a target, then press your thumb to your index finger to play";
        else
            how = "aim between thumb and index finger, then pinch to play";

        stage.setOverlayText (proc.currentOverlay() == Overlay::Drums
                                ? "Drums: " + how + " - both rails carry the same kit."
                                : how.substring (0, 1).toUpperCase() + how.substring (1) + ".");
    }

    repaint (headerArea);
    repaint (footerArea);
}

void SightLineEditor::triggerTestNote()
{
    // A second click before the first note ended: finish that one first, or
    // it would never get its note-off if the overlay or channel changed.
    if (testNoteOffTicks > 0)
        proc.playPreviewNote (testNote, testChannel, -1);

    const bool drums = proc.currentOverlay() == Overlay::Drums;
    testNote = drums ? 38 : 60;
    testChannel = drums ? proc.drumChannel() : proc.melodyChannel();

    proc.playPreviewNote (testNote, testChannel, 100);
    testNoteOffTicks = 7;   // ~230 ms at 30 Hz
}

//==============================================================================
void SightLineEditor::paintLogo (juce::Graphics& g, juce::Rectangle<float> logo)
{
    ioline::branding::drawLogoMark (g, logo);
}

void SightLineEditor::paint (juce::Graphics& g)
{
    using namespace ui;

    g.fillAll (pal::bg);

    auto h = headerArea.toFloat();
    g.setColour (pal::header);
    g.fillRoundedRectangle (h, 14.0f);

    paintLogo (g, { h.getX() + 13.0f, h.getCentreY() - 15.0f, 30.0f, 30.0f });

    g.setColour (pal::text);
    g.setFont (uiFont (18.0f, true));
    g.drawText ("sightline", h.withTrimmedLeft (52.0f).withWidth (170.0f),
                juce::Justification::centredLeft, false);

    g.setColour (pal::dim);
    g.setFont (uiFont (11.0f));
    g.drawText ("gesture  >  midi", h.withTrimmedLeft (145.0f).withWidth (170.0f),
                juce::Justification::centredLeft, false);

    // ---- right-hand readout: what is being played, and where it goes -----
    const auto overlay = proc.currentOverlay();
    const bool drums = overlay == Overlay::Drums;
    const int channel = drums ? proc.drumChannel() : proc.melodyChannel();
    const bool cameraLive = cameraIndexOpened >= 0 || cameraLiveForTesting;

    const auto dot = juce::String::charToString ((juce::juce_wchar) 0x00b7);
    juce::String readout = juce::String (overlayName (overlay)).toUpperCase();

    if (overlay == Overlay::Scale)
        readout << "  " << juce::String (keyName (proc.currentRoot(), proc.currentScale()).c_str()).toUpperCase();

    readout << "   " << dot << "   CH " << channel;

    const auto readoutFont = uiFont (11.0f, true);
    auto readoutArea = h.withTrimmedRight (audioButton.isVisible() ? 194.0f : 22.0f);
    const float readoutW = textWidth (readoutFont, readout);

    g.setColour (pal::text.withAlpha (0.85f));
    g.setFont (readoutFont);
    g.drawText (readout, readoutArea.removeFromRight (readoutW + 2.0f),
                juce::Justification::centredRight, false);

    // Status dot: the overlay colour while the camera is live, pink without one.
    readoutArea.removeFromRight (10.0f);
    g.setColour (cameraLive ? accentFor (overlay) : pal::pink);
    g.fillEllipse (readoutArea.removeFromRight (8.0f).withSizeKeepingCentre (8.0f, 8.0f));

    // ---- footer ----------------------------------------------------------
    auto f = footerArea.toFloat();
    g.setColour (pal::header);
    g.fillRoundedRectangle (f, 12.0f);

    const juce::String footerText = cameraLive
        ? "Camera is live. Hands are read on this machine; nothing is recorded or sent anywhere."
        : "No camera: open Setup and pick one. Windows may ask for permission the first time.";

    const juce::String timing = "DWELL " + juce::String (juce::roundToInt (proc.dwellMs())) + " ms   "
                              + dot + "   RELEASE "
                              + juce::String (juce::roundToInt (proc.releaseMs())) + " ms";

    g.setColour (pal::text.withAlpha (0.55f));
    g.setFont (uiFont (10.5f, true));
    g.drawText (timing, f.withTrimmedRight (18.0f), juce::Justification::centredRight, false);

    g.setColour (pal::dim);
    g.setFont (uiFont (11.0f));
    g.drawText (footerText, f.withTrimmedLeft (16.0f).withTrimmedRight (240.0f),
                juce::Justification::centredLeft, true);
}

void SightLineEditor::resized()
{
    auto b = getLocalBounds();

    headerArea = b.removeFromTop (56).reduced (14, 8);
    b.removeFromTop (6);
    tabs.setBounds (b.removeFromTop (48).reduced (14, 0));
    b.removeFromTop (10);
    footerArea = b.removeFromBottom (48).reduced (14, 4);
    b.removeFromTop (2);

    contentArea = b.reduced (14, 0);

    stage.setBounds (contentArea);
    keyPage.setBounds (contentArea);
    setupPage.setBounds (contentArea);

    audioButton.setBounds (headerArea.getRight() - 170, headerArea.getCentreY() - 13, 156, 26);
}

} // namespace sl
