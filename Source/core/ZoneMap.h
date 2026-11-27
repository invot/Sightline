#pragma once

#include "Scales.h"
#include "Types.h"

namespace sl
{

/** The two translucent rails and the targets drawn on them, in display
    coordinates (0..1, origin top-left, x already mirrored if the view is). */
struct ZoneLayout
{
    std::vector<Zone> zones;
    int leftBaseNote = 48;
    int rightBaseNote = 60;
};

/** Builds the targets for one overlay. Left rail is the lower octave, right rail
    the one above it; the drums are the same kit on both sides. */
ZoneLayout buildZones (Overlay overlay, int root, ScaleType scale, int octave);

/** 8 pieces, General MIDI drum map. */
constexpr int numDrumPads = 8;
const char* drumPadName (int index) noexcept;
int         drumPadNote (int index) noexcept;

/** The rail rectangles for drawing (left, right), in display coordinates. */
Rect leftRailBounds() noexcept;
Rect rightRailBounds() noexcept;

/** Octave 0 puts the left rail at C3 (MIDI 48). */
int baseNoteForOctave (int octave) noexcept;

/** True for the five short keys of an octave. */
bool isBlackKey (int note) noexcept;

} // namespace sl
