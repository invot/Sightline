#include "ZoneMap.h"

#include <algorithm>

namespace sl
{

namespace
{
    // General MIDI percussion, so a DAW drum kit answers out of the box.
    constexpr const char* drumNames[numDrumPads] =
    {
        "Kick", "Snare", "Closed Hat", "Open Hat", "Clap", "Low Tom", "Crash", "Ride"
    };

    constexpr int gmDrumNotes[numDrumPads] =
    {
        36, 38, 42, 46, 39, 45, 49, 51
    };

    constexpr float railInsetX   = 0.016f;
    constexpr float railWidth    = 0.150f;
    constexpr float railTop      = 0.060f;
    constexpr float railHeight   = 0.880f;

    /** One row per semitone, counted up from the bottom of the rail, so the
        overlay reads like a keyboard standing on its side. */
    Rect rowBounds (const Rect& rail, int row, int numRows) noexcept
    {
        // Both edges come from the same expression, so neighbouring rows share
        // an exact boundary instead of overlapping by a rounding error.
        const auto edge = [&rail, numRows] (int i) noexcept
        {
            return rail.getBottom() - (float) i * rail.h / (float) numRows;
        };

        return { rail.x, edge (row + 1), rail.w, edge (row) - edge (row + 1) };
    }
}

bool isBlackKey (int note) noexcept
{
    const int pc = ((note % 12) + 12) % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

Rect leftRailBounds() noexcept
{
    return { railInsetX, railTop, railWidth, railHeight };
}

Rect rightRailBounds() noexcept
{
    return { 1.0f - railInsetX - railWidth, railTop, railWidth, railHeight };
}

int baseNoteForOctave (int octave) noexcept
{
    return 12 * (4 + octave);   // octave 0 -> C3 (48)
}

const DrumNotes& defaultDrumNotes() noexcept
{
    static const DrumNotes notes = []
    {
        DrumNotes n {};

        for (int i = 0; i < numDrumPads; ++i)
            n[(size_t) i] = gmDrumNotes[i];

        return n;
    }();

    return notes;
}

const char* drumPadName (int index) noexcept
{
    return drumNames[std::clamp (index, 0, numDrumPads - 1)];
}

int drumPadNote (int index) noexcept
{
    return defaultDrumNotes()[(size_t) std::clamp (index, 0, numDrumPads - 1)];
}

ZoneLayout buildZones (Overlay overlay, int root, ScaleType scale, int octave,
                       const DrumNotes& drumNotes)
{
    ZoneLayout layout;
    const int base = baseNoteForOctave (octave);
    layout.leftBaseNote  = base;
    layout.rightBaseNote = base + 12;

    const Rect rails[2] = { leftRailBounds(), rightRailBounds() };

    for (int side = 0; side < 2; ++side)
    {
        const Rect rail = rails[side];
        const int sideBase = side == 0 ? layout.leftBaseNote : layout.rightBaseNote;

        switch (overlay)
        {
            case Overlay::Keys:
            {
                for (int row = 0; row < 12; ++row)
                {
                    Zone z;
                    z.side = side;
                    z.note = sideBase + row;
                    z.blackKey = isBlackKey (z.note);
                    z.label = midiNoteName (z.note);

                    auto r = rowBounds (rail, row, 12);

                    if (z.blackKey)
                    {
                        // The short keys hug the inner edge of the rail, the way
                        // black keys sit inside a piano's white row.
                        const float w = rail.w * 0.66f;
                        r.x = side == 0 ? rail.getRight() - w : rail.x;
                        r.w = w;
                    }

                    z.bounds = r;
                    layout.zones.push_back (z);
                }

                break;
            }

            case Overlay::Scale:
            {
                auto notes = notesInRange (sideBase, sideBase + 11, root, scale);

                if (notes.empty())
                    notes.push_back (sideBase);

                const int n = (int) notes.size();

                for (int i = 0; i < n; ++i)
                {
                    Zone z;
                    z.side = side;
                    z.note = notes[(size_t) i];
                    z.label = midiNoteName (z.note);
                    // Scale degree (1 = the root), not the semitone offset.
                    z.subLabel = std::to_string (scaleDegree (z.note, root, scale));
                    z.bounds = rowBounds (rail, i, n);
                    layout.zones.push_back (z);
                }

                break;
            }

            case Overlay::Drums:
            {
                for (int i = 0; i < numDrumPads; ++i)
                {
                    Zone z;
                    z.side = side;
                    z.note = drumNotes[(size_t) i];
                    z.label = drumPadName (i);
                    z.subLabel = std::to_string (z.note);
                    z.bounds = rowBounds (rail, i, numDrumPads);
                    layout.zones.push_back (z);
                }

                break;
            }
        }
    }

    return layout;
}

} // namespace sl
