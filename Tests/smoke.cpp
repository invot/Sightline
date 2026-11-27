// Console smoke test: builds the plugin and its editor without a DAW, drives the
// tracker with synthetic camera frames, and checks that MIDI comes out.

#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace
{
    int failures = 0;

    void check (bool ok, const char* what)
    {
        std::fprintf (stderr, "%s  %s\n", ok ? "[ok]  " : "[FAIL]", what);

        if (! ok)
            ++failures;
    }

    /** A drawn "webcam" frame: a dim room, optionally with a hand out at the
        left rail. Used instead of a real camera in tests and snapshots. Pass a
        negative handX for the empty room alone. */
    juce::Image makeCameraImage (int w, int h, float handX = -1.0f, float handY = -1.0f)
    {
        juce::Image img (juce::Image::RGB, w, h, true);
        juce::Graphics g (img);

        juce::ColourGradient wall (juce::Colour (0xff2c3238), 0.0f, 0.0f,
                                   juce::Colour (0xff14171a), 0.0f, (float) h, false);
        g.setGradientFill (wall);
        g.fillAll();

        juce::ColourGradient glow (juce::Colour (0xff55606a).withAlpha (0.55f), (float) w * 0.3f, 40.0f,
                                   juce::Colours::transparentBlack, (float) w * 0.7f, (float) h, true);
        g.setGradientFill (glow);
        g.fillRect (0, 0, w, h);

        // The hand out at the rail, plus a forearm running off the edge.
        if (handX >= 0.0f)
        {
            const float hx = handX * (float) w;
            const float hy = handY * (float) h;

            g.setColour (juce::Colour (0xffcf9d80));
            g.fillRoundedRectangle (hx - 60.0f, hy + 30.0f, 130.0f, 54.0f, 24.0f);
            g.fillEllipse (hx - 46.0f, hy - 40.0f, 92.0f, 96.0f);

            juce::ColourGradient fingers (juce::Colour (0xffe0b295), hx, hy,
                                          juce::Colour (0xffc08e73), hx + 40.0f, hy + 40.0f, false);
            g.setGradientFill (fingers);
            g.fillEllipse (hx - 34.0f, hy + 6.0f, 30.0f, 62.0f);
            g.fillEllipse (hx + 6.0f, hy + 10.0f, 28.0f, 58.0f);
        }

        g.setColour (juce::Colours::black.withAlpha (0.18f));
        g.fillRect (0, 0, w, 7);
        g.fillRect (0, h - 7, w, 7);

        return img;
    }

    int countNoteOns (const juce::MidiBuffer& midi)
    {
        int n = 0;

        for (const auto meta : midi)
            if (meta.getMessage().isNoteOn())
                ++n;

        return n;
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::fprintf (stderr, "[1] JUCE initialised\n");

#if JUCE_WINDOWS
    // JUCE reads the process environment block, not the CRT copy that
    // _putenv_s updates, so the harness has to set it the Win32 way.
    SetEnvironmentVariableW (L"SIGHTLINE_NO_CAMERA", L"1");
#endif

    // ---- gestures become MIDI --------------------------------------------
    {
        sl::SightLineProcessor proc;
        proc.prepareToPlay (48000.0, 512);
        std::fprintf (stderr, "[2] processor constructed (name=%s)\n", proc.getName().toRawUTF8());

        const auto zones = proc.getZones();
        check (zones.size() == 24, "[3] keys overlay builds 24 targets");

        const auto zone = zones[3];
        sl::HandSample hand;
        hand.detected = true;
        hand.x = zone.bounds.getCentreX();
        hand.y = zone.bounds.getCentreY();

        juce::AudioBuffer<float> audio (2, 512);
        juce::MidiBuffer midi;

        const auto pump = [&] (double t)
        {
            auto h = hand;
            proc.updateHands (h, {}, t);
            audio.clear();
            midi.clear();
            proc.processBlock (audio, midi);
            return countNoteOns (midi);
        };

        const double t0 = juce::Time::getMillisecondCounterHiRes();
        check (pump (t0) == 0, "[4] no note while the hand is still arming");
        check (pump (t0 + 50.0) == 0, "[4a] still arming after 50 ms");

        const int fired = pump (t0 + 130.0);
        int firedNote = -1;

        for (const auto meta : midi)
            if (meta.getMessage().isNoteOn())
                firedNote = meta.getMessage().getNoteNumber();

        check (fired == 1, "[5] the note fires once the dwell is met");
        check (firedNote == zone.note, "[5a] the note is the one under the hand");

        // Notes stay inside the plugin's channel, nothing else is touched.
        bool rightChannel = true;

        for (const auto meta : midi)
            if (meta.getMessage().isNoteOn())
                rightChannel = rightChannel && meta.getMessage().getChannel() == proc.melodyChannel();

        check (rightChannel, "[5b] notes go out on the melody channel");

        // Leaving ends the note.
        juce::MidiBuffer offMidi;
        juce::AudioBuffer<float> offAudio (2, 512);
        sl::HandSample gone;
        proc.updateHands (gone, {}, t0 + 400.0);
        offAudio.clear();
        offMidi.clear();
        proc.processBlock (offAudio, offMidi);

        int offs = 0;

        for (const auto meta : offMidi)
            offs += meta.getMessage().isNoteOff() ? 1 : 0;

        check (offs >= 1, "[6] leaving the target sends a note off");

        // Transparent in a DAW: audio must arrive untouched.
        juce::AudioBuffer<float> pass (2, 256);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 256; ++i)
                pass.setSample (ch, i, 0.25f);

        juce::MidiBuffer passMidi;
        proc.processBlock (pass, passMidi);
        check (std::abs (pass.getSample (0, 100) - 0.25f) < 1.0e-6f,
               "[7] the plugin passes audio through untouched");

        // Drums: the overlay switches channel and note.
        if (auto* overlayParam = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter ("overlay")))
        {
            *overlayParam = 2;
            proc.syncZones();
        }

        check (proc.getZones().size() == (size_t) sl::numDrumPads * 2, "[8] drums overlay builds 16 pads");

        const auto kickPad = proc.getZones()[0];
        sl::HandSample drumHand;
        drumHand.detected = true;
        drumHand.x = kickPad.bounds.getCentreX();
        drumHand.y = kickPad.bounds.getCentreY();

        juce::MidiBuffer drumMidi;
        juce::AudioBuffer<float> drumAudio (2, 512);
        // The pad needs its dwell, like any other target.
        proc.updateHands (drumHand, {}, t0 + 600.0);
        proc.updateHands (drumHand, {}, t0 + 700.0);
        drumAudio.clear();
        drumMidi.clear();
        proc.processBlock (drumAudio, drumMidi);

        bool drumOk = false;

        for (const auto meta : drumMidi)
        {
            const auto m = meta.getMessage();

            if (m.isNoteOn() && m.getChannel() == proc.drumChannel() && m.getNoteNumber() == kickPad.note)
                drumOk = true;
        }

        check (drumOk, "[9] a drum pad plays its GM note on the drum channel");

        // ---- per-pad mapping, MIDI learn and state --------------------------
        proc.setDrumNote (0, 41);
        check (proc.getZones()[0].note == 41, "[9a] a drum pad takes a learned note");
        check (proc.getZones()[(size_t) sl::numDrumPads].note == 41,
               "[9b] the other rail plays the same mapping");

        proc.startDrumLearn (2);

        juce::MidiBuffer learnMidi;
        learnMidi.addEvent (juce::MidiMessage::noteOn (1, 55, (juce::uint8) 100), 0);
        juce::AudioBuffer<float> learnAudio (2, 512);
        learnAudio.clear();
        proc.processBlock (learnAudio, learnMidi);

        int learnedPad = -1, learnedNote = -1;
        check (proc.takeLearnedNote (learnedPad, learnedNote)
                   && learnedPad == 2 && learnedNote == 55,
               "[9c] MIDI learn captures the next incoming note");

        proc.setDrumNote (learnedPad, learnedNote);
        check (proc.getZones()[2].note == 55, "[9d] the learned note reaches the pad");

        juce::MemoryBlock pluginState;
        proc.getStateInformation (pluginState);

        sl::SightLineProcessor restored;
        restored.setStateInformation (pluginState.getData(), (int) pluginState.getSize());

        check (restored.getZones().size() > 2
                   && restored.getZones()[0].note == 41
                   && restored.getZones()[2].note == 55,
               "[9e] drum mapping survives save and reload");

        restored.resetDrumNote (0);
        check (restored.getZones()[0].note == sl::drumPadNote (0),
               "[9f] resetting a pad returns it to its GM note");

        proc.updateHands ({}, {}, t0 + 900.0);
        proc.releaseResources();
    }

    // ---- the standalone app makes sound on its own ------------------------
    {
        juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Standalone);
        sl::SightLineProcessor app;
        juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);

        app.prepareToPlay (48000.0, 512);
        check (app.isStandaloneApp(), "[10] the standalone identifies itself");

        if (auto* overlayParam = dynamic_cast<juce::AudioParameterChoice*> (app.apvts.getParameter ("overlay")))
        {
            *overlayParam = 2;
            app.syncZones();
        }

        const auto pad = app.getZones()[1];   // snare
        sl::HandSample hand;
        hand.detected = true;
        hand.x = pad.bounds.getCentreX();
        hand.y = pad.bounds.getCentreY();

        juce::AudioBuffer<float> audio (2, 512);
        juce::MidiBuffer midi;

        const double now = juce::Time::getMillisecondCounterHiRes();
        app.updateHands (hand, {}, now);
        app.updateHands (hand, {}, now + 120.0);

        float peak = 0.0f;

        for (int block = 0; block < 6; ++block)
        {
            audio.clear();
            midi.clear();
            app.processBlock (audio, midi);
            peak = std::max (peak, audio.getMagnitude (0, 512));
        }

        check (peak > 0.001f, "[11] the standalone plays its built-in kit");
        app.releaseResources();
    }

    // ---- the editor -------------------------------------------------------
    if (auto* editor = [] {
            auto* p = new sl::SightLineProcessor();
            return static_cast<juce::AudioProcessorEditor*> (p->createEditor());
        }())
    {
        std::fprintf (stderr, "[12] editor constructed (%d x %d)\n", editor->getWidth(), editor->getHeight());

        auto* proc = static_cast<sl::SightLineProcessor*> (editor->getAudioProcessor());
        auto* sight = dynamic_cast<sl::SightLineEditor*> (editor);

        const bool model = sight != nullptr && sight->usesHandModelForTesting();
        std::fprintf (stderr, "[12a] hand tracker: %s\n", model ?"landmark model" : "basic (no ONNX Runtime)");

        const auto photoFile = juce::File::getCurrentWorkingDirectory().getChildFile ("docs/media/camera-frame.jpg");
        const auto photo = juce::ImageFileFormat::loadFrom (photoFile);

        if (sight != nullptr && model)
        {
            // A real photo of two open hands. The landmark model can't be fooled
            // by a drawn blob, so the checks use it instead.
            check (photo.isValid(), "[13] the test photo loads (run from the repo root)");

            if (auto* pinch = dynamic_cast<juce::AudioParameterBool*> (proc->apvts.getParameter ("pinch")))
                *pinch = true;

            proc->syncZones();

            double total = 0.0;
            const int runs = 20;

            for (int i = 0; i < runs; ++i)
            {
                const auto t0 = juce::Time::getMillisecondCounterHiRes();
                sight->feedFrame (photo);
                total += juce::Time::getMillisecondCounterHiRes() - t0;
                juce::Thread::sleep (10);
            }

            std::fprintf (stderr, "[13a] %d x %d photo: %.1f ms a frame including tracking\n",
                          photo.getWidth(), photo.getHeight(), total / runs);

            const auto live = proc->getLiveState();
            check (live.handSeen[0] && live.handSeen[1], "[13] the model finds both hands in the photo");
            check (live.hasPinch[0] && ! live.pinching[0] && ! live.pinching[1],
                   "[13b] both hands read as open, not pinching");
            check (live.activeNote[0] < 0 && live.activeNote[1] < 0,
                   "[14] open hands under pinch-to-play hold no note");

            // Mirrored display: the photo's right hand (x ~ 0.74) is drawn on
            // the left. The cursor sits between thumb and index tip.
            check (std::abs (live.handX[0] - (1.0f - 0.74f)) < 0.12f && std::abs (live.handX[1] - (1.0f - 0.30f)) < 0.12f,
                   "[14b] each cursor sits on its hand");

            // An empty room: no hand, no cursor, no note.
            const auto empty = makeCameraImage (960, 540);

            for (int i = 0; i < 4; ++i)
                sight->feedFrame (empty);

            const auto after = proc->getLiveState();
            check (! after.handSeen[0] && ! after.handSeen[1], "[14c] an empty room shows no hand");

            // Pointing-finger aim: open hands aren't pointing, so nothing aims.
            if (auto* aim = dynamic_cast<juce::AudioParameterChoice*> (proc->apvts.getParameter ("aim")))
            {
                *aim = 1;

                for (int i = 0; i < 4; ++i)
                    sight->feedFrame (photo);

                const auto pointing = proc->getLiveState();
                check (! pointing.handSeen[0] && ! pointing.handSeen[1],
                       "[14d] pointing-finger aim ignores open hands");
                *aim = 0;
            }
        }
        else if (sight != nullptr)
        {
            const auto frame = makeCameraImage (960, 540, 0.09f, 0.55f);
            sight->feedFrame (frame);

            juce::Thread::sleep (140);
            sight->feedFrame (frame);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (120);

            check (proc->getLiveState().handSeen[1], "[13] the editor tracks the hand in the frame");
            check (proc->getLiveState().activeNote[1] >= 0, "[14] the hand is holding a note");
            check (sight->getStageForTesting()->hasTrackingMask(),
                   "[14a] the tracking test receives the detector's mask");
        }

        const auto snapDir = juce::SystemStats::getEnvironmentVariable ("SIGHTLINE_SNAPSHOT_DIR", {});

        // The checks above need a hand for the tracker to find; the pictures
        // show a stand-in camera frame instead, so the stage has a picture
        // behind the rails the way a live camera would.
        auto snapshotFrame = makeCameraImage (960, 540);

        if (const auto photo = juce::File::getCurrentWorkingDirectory()
                                   .getChildFile ("docs/media/camera-frame.jpg");
            photo.existsAsFile())
            if (const auto loaded = juce::ImageFileFormat::loadFrom (photo); loaded.isValid())
                snapshotFrame = loaded;

        const auto showCameraFrame = [&]
        {
            if (sight != nullptr)
                if (auto* stage = sight->getStageForTesting())
                    stage->setFrame (snapshotFrame);
        };

        if (snapDir.isNotEmpty())
        {
            // The shots show a live-looking feed, so the chrome should agree.
            if (sight != nullptr)
            {
                sight->setCameraLiveForTesting (true);

                if (auto* stage = sight->getStageForTesting())
                    stage->setCameraStatus ("webcam", true);
            }

            auto save = [&] (const juce::String& name)
            {
                proc->setCameraFps (30.0f);
                const auto image = editor->createComponentSnapshot (editor->getLocalBounds());
                juce::File file (snapDir + "/" + name + ".png");
                file.deleteFile();
                juce::FileOutputStream out (file);
                juce::PNGImageFormat().writeImageToStream (image, out);
            };

            if (sight != nullptr)
            {
                if (auto* tabs = sight->getTabBarForTesting())
                {
                    tabs->setSelected (0, true);
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (60);
                    showCameraFrame();
                    save ("play");

                    // The same frame with the tracking mask over it.
                    if (auto* stage = sight->getStageForTesting())
                    {
                        stage->setTrackingDebug (true);
                        sight->feedFrame (model && photo.isValid() ? photo : makeCameraImage (960, 540, 0.09f, 0.55f));
                        juce::MessageManager::getInstance()->runDispatchLoopUntil (60);
                        save ("play-tracking");
                        stage->setTrackingDebug (false);
                    }

                    // The same frame with the drum kit on the rails.
                    if (auto* overlayParam = dynamic_cast<juce::AudioParameterChoice*> (proc->apvts.getParameter ("overlay")))
                    {
                        *overlayParam = 2;
                        proc->syncZones();

                        const auto frame = makeCameraImage (960, 540, 0.09f, 0.55f);
                        sight->feedFrame (frame);
                        juce::Thread::sleep (140);
                        sight->feedFrame (frame);
                        juce::MessageManager::getInstance()->runDispatchLoopUntil (60);
                        showCameraFrame();
                        save ("play-drums");

                        // Armed MIDI learn: the pad is highlighted and the
                        // banner says what to do.
                        proc->startDrumLearn (1);
                        juce::MessageManager::getInstance()->runDispatchLoopUntil (60);
                        showCameraFrame();
                        save ("play-learn");
                        proc->cancelDrumLearn();

                        *overlayParam = 0;
                        proc->syncZones();
                    }

                    tabs->setSelected (1, true);
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (60);
                    save ("key");

                    tabs->setSelected (2, true);
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (60);
                    save ("setup");
                }
            }

            std::fprintf (stderr, "[15] snapshots written to %s\n", snapDir.toRawUTF8());
        }

        delete editor;
    }

    std::fprintf (stderr, "%s\n", failures == 0 ? "[16] all smoke checks passed" : "[16] SMOKE CHECKS FAILED");
    return failures == 0 ? 0 : 1;
}
