// JUCE-free checks for the parts that decide what gets played: the zone map,
// the dwell gate, the webcam detector and the standalone instrument.

#include "core/GestureEngine.h"
#include "core/HandLandmarker.h"
#include "core/HandTracker.h"
#include "core/OrtModel.h"
#include "core/InternalSynth.h"
#include "core/ZoneMap.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace
{
    int failures = 0;

    void check (bool ok, const char* what)
    {
        std::printf ("%s  %s\n", ok ? "[ok]  " : "[FAIL]", what);

        if (! ok)
            ++failures;
    }

    /** A frame with a skin-coloured blob at (nx, ny), on a plain wall. */
    std::vector<uint8_t> makeFrame (int w, int h, float nx, float ny, int blobRadius,
                                    bool blob = true)
    {
        std::vector<uint8_t> rgb ((size_t) w * (size_t) h * 3u, 0);
        const int cx = (int) (nx * (float) w);
        const int cy = (int) (ny * (float) h);

        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                const size_t i = ((size_t) y * (size_t) w + (size_t) x) * 3u;

                // A cool grey wall, so the skin test has something to reject.
                rgb[i] = 74;
                rgb[i + 1] = 78;
                rgb[i + 2] = 82;

                if (blob)
                {
                    const int dx = x - cx, dy = y - cy;

                    if (dx * dx + dy * dy <= blobRadius * blobRadius)
                    {
                        rgb[i] = 214;
                        rgb[i + 1] = 166;
                        rgb[i + 2] = 138;
                    }
                }
            }
        }

        return rgb;
    }
}

int main (int, char** argv)
{
    // The runtime is copied next to this executable.
    std::wstring exeDir;
    {
        std::string self = argv[0];
        const auto slash = self.find_last_of ("/\\");
        self = slash == std::string::npos ? std::string (".\\") : self.substr (0, slash + 1);
        exeDir.assign (self.begin(), self.end());
    }
    (void) exeDir;

    std::printf ("sightline core tests\n");

    // ---- the keyboard overlay ---------------------------------------------
    {
        const auto layout = sl::buildZones (sl::Overlay::Keys, 0, sl::ScaleType::Major, 0);

        int left = 0, right = 0, blacks = 0;

        for (const auto& z : layout.zones)
        {
            (z.side == 0 ? left : right)++;

            if (z.blackKey)
                ++blacks;
        }

        check (layout.zones.size() == 24, "keys: 24 targets, one octave per rail");
        check (left == 12 && right == 12, "keys: 12 targets on each side");
        check (blacks == 10, "keys: 5 black keys per octave");
        check (layout.leftBaseNote == 48 && layout.rightBaseNote == 60,
               "keys: octave 0 puts the left rail at C3 and the right at C4");

        bool ascending = true;

        for (int i = 0; i < 12; ++i)
            ascending = ascending && layout.zones[(size_t) i].note == 48 + i;

        check (ascending, "keys: the rows run up the semitone scale");

        // No two targets overlap on a rail: a hand can only be on one.
        bool overlap = false;

        for (size_t i = 0; i < layout.zones.size(); ++i)
            for (size_t j = i + 1; j < layout.zones.size(); ++j)
                if (layout.zones[i].side == layout.zones[j].side
                    && layout.zones[i].bounds.intersects (layout.zones[j].bounds)
                    && ! layout.zones[j].blackKey)
                    overlap = true;

        check (! overlap, "keys: white targets do not overlap each other");
    }

    // ---- the scale overlay --------------------------------------------------
    {
        const auto layout = sl::buildZones (sl::Overlay::Scale, 0, sl::ScaleType::Major, 1);

        bool allInKey = true;
        int left = 0, right = 0;

        for (const auto& z : layout.zones)
        {
            allInKey = allInKey && sl::noteInScale (z.note, 0, sl::ScaleType::Major);
            (z.side == 0 ? left : right)++;
        }

        check (left == 7 && right == 7, "scale: seven targets per rail in C major");
        check (allInKey, "scale: every target is in the key");
        check (layout.leftBaseNote == 60, "scale: the octave control moves the rails");

        const auto pentatonic = sl::buildZones (sl::Overlay::Scale, 7, sl::ScaleType::PentatonicMinor, 0);
        check (pentatonic.zones.size() == 10, "scale: pentatonic puts five targets on each rail");

        const auto fSharp = sl::buildZones (sl::Overlay::Scale, 6, sl::ScaleType::Major, 0);
        bool fSharpOk = true;

        for (const auto& z : fSharp.zones)
            fSharpOk = fSharpOk && sl::noteInScale (z.note, 6, sl::ScaleType::Major);

        check (fSharpOk, "scale: a key other than C maps correctly");
    }

    // ---- the drum overlay ---------------------------------------------------
    {
        const auto layout = sl::buildZones (sl::Overlay::Drums, 0, sl::ScaleType::Major, 0);
        check (layout.zones.size() == (size_t) sl::numDrumPads * 2, "drums: eight pads on each rail");

        bool gmNotes = true;

        for (const auto& z : layout.zones)
            gmNotes = gmNotes && z.note >= 35 && z.note <= 59;

        check (gmNotes, "drums: targets use General MIDI percussion numbers");

        // A learned note replaces the GM default on both rails.
        auto custom = sl::defaultDrumNotes();
        custom[0] = 41;
        const auto mapped = sl::buildZones (sl::Overlay::Drums, 0, sl::ScaleType::Major, 0, custom);

        check (mapped.zones[0].note == 41
                   && mapped.zones[(size_t) sl::numDrumPads].note == 41,
               "drums: a learned note applies to both rails");
        check (mapped.zones[0].subLabel == "41", "drums: the pad shows its learned note");
        check (mapped.zones[1].note == sl::drumPadNote (1),
               "drums: pads without a mapping keep their GM note");
    }

    // ---- dwell gating -------------------------------------------------------
    {
        sl::GestureEngine engine;
        engine.setZones (sl::buildZones (sl::Overlay::Keys, 0, sl::ScaleType::Major, 0).zones);
        engine.setParams (100, 1, 10, 90.0, 140.0, false);

        const auto& zones = engine.getZones();
        const auto& zone = zones[3];   // D#3 on the left rail

        sl::HandSample hand;
        hand.detected = true;
        hand.x = zone.bounds.getCentreX();
        hand.y = zone.bounds.getCentreY();

        std::vector<sl::NoteEvent> events;

        engine.update (hand, {}, 1000.0, events);
        check (events.empty(), "gate: nothing fires the instant a hand arrives");

        engine.update (hand, {}, 1040.0, events);
        check (events.empty(), "gate: still waiting halfway through the dwell");

        engine.update (hand, {}, 1100.0, events);
        check (events.size() == 1 && events[0].type == sl::NoteEvent::noteOn
                   && events[0].note == zone.note,
               "gate: the note fires once the dwell is met");

        events.clear();
        engine.update (hand, {}, 1400.0, events);
        check (events.empty(), "gate: a held hand does not retrigger");

        events.clear();
        sl::HandSample away;
        away.detected = false;
        engine.update (away, {}, 1450.0, events);
        check (events.empty(), "gate: the release window holds the note briefly");

        engine.update (away, {}, 1560.0, events);
        check (events.size() == 1 && events[0].type == sl::NoteEvent::noteOff,
               "gate: leaving the target ends the note");

        // Both sides at once.
        sl::GestureEngine two;
        two.setZones (sl::buildZones (sl::Overlay::Keys, 0, sl::ScaleType::Major, 0).zones);
        two.setParams (90, 1, 10, 50.0, 120.0, false);

        const auto& zs = two.getZones();
        sl::HandSample lh, rh;
        lh.detected = true;
        lh.x = zs[0].bounds.getCentreX();
        lh.y = zs[0].bounds.getCentreY();
        rh.detected = true;
        rh.x = zs[13].bounds.getCentreX();
        rh.y = zs[13].bounds.getCentreY();

        std::vector<sl::NoteEvent> both;
        two.update (lh, rh, 0.0, both);
        two.update (lh, rh, 80.0, both);

        check (both.size() == 2, "gate: both hands can hold a note at once");
        check (both[0].note != both[1].note, "gate: the two hands play different notes");

        // Drums go out on the drum channel, on General MIDI notes.
        sl::GestureEngine drums;
        drums.setZones (sl::buildZones (sl::Overlay::Drums, 0, sl::ScaleType::Major, 0).zones);
        drums.setParams (110, 1, 10, 0.0, 100.0, true);

        const auto& dz = drums.getZones()[0];
        sl::HandSample dh;
        dh.detected = true;
        dh.x = dz.bounds.getCentreX();
        dh.y = dz.bounds.getCentreY();

        std::vector<sl::NoteEvent> drumEvents;
        drums.update (dh, {}, 0.0, drumEvents);
        check (drumEvents.size() == 1 && drumEvents[0].channel == 10,
               "drums: a pad plays on the drum channel");
        check (drumEvents.size() == 1 && drumEvents[0].note == sl::drumPadNote (0),
               "drums: a pad plays its GM note");
    }

    // ---- the detector -------------------------------------------------------
    {
        const int w = 320, h = 240;
        sl::HandTracker tracker;
        tracker.prepare (w, h);

        auto frame = makeFrame (w, h, 0.09f, 0.55f, 26);
        tracker.analyse (frame.data(), w, h, w * 3, 0.5f);

        const auto left = tracker.side (0);
        check (left.detected, "detector: finds a hand in the left rail");
        check (std::abs (left.x - 0.09f) < 0.05f && std::abs (left.y - 0.55f) < 0.06f,
               "detector: the hand position is close to where it was drawn");
        check (! tracker.side (1).detected, "detector: the empty rail stays empty");

        auto rightFrame = makeFrame (w, h, 0.91f, 0.3f, 24);
        tracker.analyse (rightFrame.data(), w, h, w * 3, 0.5f);
        check (tracker.side (1).detected && std::abs (tracker.side (1).x - 0.91f) < 0.05f,
               "detector: finds a hand in the right rail");

        // The first quiet frame still shows the hand leaving; once the scene is
        // steady, a bare wall has to read as nothing at all.
        auto empty = makeFrame (w, h, 0.5f, 0.5f, 0, false);
        tracker.analyse (empty.data(), w, h, w * 3, 0.5f);
        tracker.analyse (empty.data(), w, h, w * 3, 0.5f);
        check (! tracker.side (0).detected && ! tracker.side (1).detected,
               "detector: a bare wall triggers nothing");
    }

    // ---- pinch to play ------------------------------------------------------
    {
        sl::GestureEngine engine;
        engine.setZones (sl::buildZones (sl::Overlay::Keys, 0, sl::ScaleType::Major, 0).zones);
        engine.setParams (100, 1, 10, 90.0, 140.0, false);
        engine.setPinchToPlay (true);

        const auto& zone = engine.getZones()[3];

        sl::HandSample hand;
        hand.detected = true;
        hand.hasPinch = true;
        hand.x = zone.bounds.getCentreX();
        hand.y = zone.bounds.getCentreY();

        std::vector<sl::NoteEvent> events;
        engine.update (hand, {}, 0.0, events);
        engine.update (hand, {}, 500.0, events);
        check (events.empty(), "pinch: an open hand resting on a target plays nothing");
        check (engine.hoveredZone (0) == 3, "pinch: the open hand still shows which target it aims at");
        check (engine.dwellProgress (0, 500.0) == 0.0f, "pinch: no dwell ring while only aiming");

        hand.pinching = true;
        engine.update (hand, {}, 520.0, events);
        check (events.size() == 1 && events[0].type == sl::NoteEvent::noteOn && events[0].note == zone.note,
               "pinch: closing the pinch plays at once");

        events.clear();
        engine.update (hand, {}, 900.0, events);
        check (events.empty(), "pinch: a held pinch holds the note");

        hand.pinching = false;
        engine.update (hand, {}, 920.0, events);
        check (events.size() == 1 && events[0].type == sl::NoteEvent::noteOff,
               "pinch: opening the pinch ends the note at once");

        // The basic tracker can't see fingers: it keeps the dwell behaviour.
        events.clear();
        sl::HandSample blob = hand;
        blob.hasPinch = false;
        engine.update (blob, {}, 2000.0, events);
        engine.update (blob, {}, 2100.0, events);
        check (events.size() == 1 && events[0].type == sl::NoteEvent::noteOn,
               "pinch: without finger tracking a resting hand still plays");
    }

    // ---- landmark model plumbing -------------------------------------------
    {
        // A straight copy: a square crop the size of the frame, no rotation.
        const int w = 64, h = 64;
        std::vector<uint8_t> rgb ((size_t) w * h * 3u);

        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                auto* p = rgb.data() + ((size_t) y * w + (size_t) x) * 3u;
                p[0] = (uint8_t) (x * 4); p[1] = (uint8_t) (y * 4); p[2] = 77;
            }

        std::vector<float> out ((size_t) w * h * 3u);
        sl::warpToTensor (rgb.data(), w, h, w * 3, 32.0f, 32.0f, 64.0f, 0.0f, 64, out.data());

        float worst = 0.0f;

        for (size_t i = 0; i < out.size(); ++i)
            worst = std::max (worst, std::abs (out[i] - (float) rgb[i] / 255.0f));

        check (worst < 0.01f, "warp: an unrotated full-frame crop copies the frame");

        // A quarter turn: the crop's "up" is the frame's right.
        sl::warpToTensor (rgb.data(), w, h, w * 3, 32.0f, 32.0f, 64.0f, 3.14159265f * 0.5f, 64, out.data());
        const float topRed = out[((size_t) 2 * 64 + 32) * 3u] * 255.0f;   // crop row 2, centre column
        check (topRed > 220.0f, "warp: a 90 degree crop puts the frame's right edge at the top");

        // Pointing: world landmarks for a hand with the index finger out and
        // the rest folded, and the same hand open.
        sl::HandPose open, point;
        const auto setPt = [] (sl::HandPose& p, int i, float x, float y)
        {
            p.wx[i] = x; p.wy[i] = y; p.wz[i] = 0.0f;
        };

        // x across the palm, y up the fingers (metres). Knuckles at y = 0.08.
        const float fingerX[4] { -0.025f, 0.0f, 0.02f, 0.038f };

        for (auto* p : { &open, &point })
        {
            setPt (*p, 0, 0.0f, 0.0f);                       // wrist
            setPt (*p, 1, -0.03f, 0.02f);  setPt (*p, 2, -0.045f, 0.04f);
            setPt (*p, 3, -0.055f, 0.06f); setPt (*p, 4, -0.06f, 0.075f);   // thumb out to the side

            for (int f = 0; f < 4; ++f)
            {
                const int base = 5 + f * 4;
                const bool out_ = p == &open || f == 0;

                setPt (*p, base, fingerX[f], 0.08f);
                setPt (*p, base + 1, fingerX[f], 0.11f);

                if (out_)
                {
                    setPt (*p, base + 2, fingerX[f], 0.13f);
                    setPt (*p, base + 3, fingerX[f], 0.15f);
                }
                else
                {
                    // Folded back down towards the palm.
                    setPt (*p, base + 2, fingerX[f], 0.09f);
                    setPt (*p, base + 3, fingerX[f], 0.07f);
                }
            }
        }

        check (sl::HandLandmarker::looksLikePointing (point), "point: index out, others folded reads as pointing");
        check (! sl::HandLandmarker::looksLikePointing (open), "point: an open hand is not pointing");
        check (sl::HandLandmarker::thumbToIndexSide (point) > sl::HandLandmarker::clickOffMetres,
               "point: a thumb held out is not a click");

        sl::HandPose pressed = point;
        pressed.wx[4] = -0.012f; pressed.wy[4] = 0.095f;   // thumb tip against the index finger
        check (sl::HandLandmarker::thumbToIndexSide (pressed) < sl::HandLandmarker::clickOnMetres,
               "point: the thumb pressed to the index finger is a click");
    }

   #if SL_HAVE_ORT && defined (SL_TEST_MODEL_DIR)
    // ---- the real models -----------------------------------------------------
    {
        const auto readFile = [] (const std::string& path)
        {
            std::ifstream in (path, std::ios::binary);
            return std::vector<char> ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
        };

        std::wstring dll = exeDir + L"onnxruntime.dll";
        std::string error;
        const bool loaded = sl::ort::loadRuntime (dll, error);
        check (loaded, ("model: ONNX Runtime loads " + error).c_str());

        const auto palmBytes = readFile (std::string (SL_TEST_MODEL_DIR) + "/palm_detection.onnx");
        const auto handBytes = readFile (std::string (SL_TEST_MODEL_DIR) + "/hand_landmark.onnx");

        auto palm = sl::ort::createModel (palmBytes.data(), palmBytes.size(), { 1, 192, 192, 3 }, error);
        auto hand = palm == nullptr ? nullptr
                  : sl::ort::createModel (handBytes.data(), handBytes.size(), { 1, 224, 224, 3 }, error);
        check (palm != nullptr && hand != nullptr, ("model: both models load " + error).c_str());

        if (palm != nullptr && hand != nullptr)
        {
            sl::HandTracker tracker;
            tracker.setLandmarker (std::make_unique<sl::HandLandmarker> (std::move (palm), std::move (hand)));

            // The old tracker saw hands in these; the model must not.
            const int w = 640, h = 480;
            const auto wall = makeFrame (w, h, 0.5f, 0.5f, 0, false);
            const auto blob = makeFrame (w, h, 0.12f, 0.5f, 60);

            std::vector<uint8_t> noise ((size_t) w * h * 3u);
            uint32_t seed = 1234567u;

            for (auto& v : noise)
            {
                seed = seed * 1664525u + 1013904223u;
                v = (uint8_t) (seed >> 24);
            }

            int ghosts = 0;

            for (int i = 0; i < 10; ++i)
            {
                const auto& f = i % 3 == 0 ? wall : (i % 3 == 1 ? blob : noise);
                tracker.analyse (f.data(), w, h, w * 3, 0.5f, i * 33.0);
                ghosts += (tracker.side (0).detected ? 1 : 0) + (tracker.side (1).detected ? 1 : 0);
            }

            check (ghosts == 0, "model: walls, skin-coloured blobs and noise report no hand");
        }
    }
   #endif

    // ---- full-frame landmark backend (Vision) -------------------------------
    {
        using Pose = sl::HandPose;
        std::array<Pose, 2> poses;
        poses[0].x[Pose::wrist] = 40.0f;
        poses[0].pinchDistance = 0.025f;
        poses[1].x[Pose::wrist] = 160.0f;
        int count = 2;
        sl::HandLandmarker tracker ([&] (const uint8_t*, int, int, int, std::array<Pose, 2>& output)
        {
            output = poses;
            return count;
        });

        const std::vector<uint8_t> frame (200 * 100 * 3);
        check (tracker.process (frame.data(), 200, 100, 600, 0.0) == 2, "full-frame model: finds both hands");
        const int id = tracker.hands().empty() ? 0 : tracker.hands()[0].id;

        poses[0].pinchDistance = 0.04f;
        std::swap (poses[0], poses[1]);
        tracker.process (frame.data(), 200, 100, 600, 33.0);
        check (! tracker.hands().empty() && tracker.hands()[0].id == id && tracker.hands()[0].pinching,
               "full-frame model: pinch hysteresis survives detector result reordering");

        count = 0;
        check (tracker.process (frame.data(), 200, 100, 600, 66.0) == 0, "full-frame model: missing hands clear immediately");
    }

    // ---- the built-in instrument -------------------------------------------
    {
        sl::InternalSynth synth;
        synth.prepare (48000.0);
        synth.noteOn (60, 100, 1);

        std::vector<float> l (4800, 0.0f), r (4800, 0.0f);
        synth.render (l.data(), r.data(), (int) l.size());

        float peak = 0.0f;

        for (size_t i = 0; i < l.size(); ++i)
            peak = std::max (peak, std::abs (l[i]));

        check (peak > 0.001f, "synth: a note makes sound");

        synth.noteOff (60, 1);

        for (int block = 0; block < 40; ++block)
        {
            // render() mixes into its buffers, like a plugin's processBlock.
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            synth.render (l.data(), r.data(), (int) l.size());
        }

        peak = 0.0f;

        for (size_t i = 0; i < l.size(); ++i)
            peak = std::max (peak, std::abs (l[i]));

        check (peak < 0.001f, "synth: the note stops after note off");

        sl::InternalSynth kit;
        kit.prepare (48000.0);
        kit.noteOn (36, 120, 10);
        l.assign (9600, 0.0f);
        r.assign (9600, 0.0f);
        kit.render (l.data(), r.data(), (int) l.size());

        peak = 0.0f;

        for (size_t i = 0; i < l.size(); ++i)
            peak = std::max (peak, std::abs (l[i]));

        check (peak > 0.01f, "drums: the kick makes sound on the drum channel");
    }

    std::printf ("%s\n", failures == 0 ? "all core tests passed" : "CORE TESTS FAILED");
    return failures == 0 ? 0 : 1;
}
