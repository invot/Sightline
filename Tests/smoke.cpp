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

    /** A drawn "webcam" frame: a dim room, a person in the middle and a hand out
        at the left rail. Used instead of a real camera in tests and snapshots. */
    juce::Image makeCameraImage (int w, int h, float handX, float handY)
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

        // Person: shoulders and head, cool and desaturated.
        g.setColour (juce::Colour (0xff3a4148));
        g.fillEllipse ((float) w * 0.5f - 92.0f, (float) h * 0.30f, 184.0f, 210.0f);

        g.setColour (juce::Colour (0xff4d565f));
        juce::Path shoulders;
        shoulders.addRoundedRectangle ((float) w * 0.5f - 230.0f, (float) h * 0.62f, 460.0f, (float) h * 0.5f, 120.0f);
        g.fillPath (shoulders);

        // Face, so the tracker has to ignore a skin-coloured centre.
        g.setColour (juce::Colour (0xffd6a689));
        g.fillEllipse ((float) w * 0.5f - 62.0f, (float) h * 0.255f, 124.0f, 148.0f);

        g.setColour (juce::Colour (0xff2b3036));
        g.fillEllipse ((float) w * 0.5f - 40.0f, (float) h * 0.245f, 80.0f, 44.0f);

        // The hand out at the rail, plus a forearm running off the edge.
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

        if (sight != nullptr)
        {
            const auto frame = makeCameraImage (960, 540, 0.09f, 0.55f);
            sight->feedFrame (frame);

            juce::Thread::sleep (140);
            sight->feedFrame (frame);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (120);
        }

        check (proc->getLiveState().handSeen[1], "[13] the editor tracks the hand in the frame");
        check (proc->getLiveState().activeNote[1] >= 0, "[14] the hand is holding a note");

        const auto snapDir = juce::SystemStats::getEnvironmentVariable ("SIGHTLINE_SNAPSHOT_DIR", {});

        if (snapDir.isNotEmpty())
        {
            auto save = [&] (const juce::String& name)
            {
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
                    save ("play");

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
                        save ("play-drums");

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
