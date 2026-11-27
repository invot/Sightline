// JUCE-free checks for the parts that decide what gets played: the zone map,
// the dwell gate, the webcam detector and the standalone instrument.

#include "core/GestureEngine.h"
#include "core/HandTracker.h"
#include "core/InternalSynth.h"
#include "core/ZoneMap.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
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

int main()
{
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
