#pragma once

#include "Types.h"

#include <array>

namespace sl
{

enum class ScaleType
{
    Chromatic = 0,
    Major,
    Minor,
    Dorian,
    Mixolydian,
    PentatonicMajor,
    PentatonicMinor,
    Blues,
    NumScales
};

const char* scaleName (ScaleType s) noexcept;

/** Bit i set = pitch class i belongs to the scale. */
uint16_t scaleMask (ScaleType s) noexcept;

/** True when `note` is in root/scale. */
bool noteInScale (int note, int root, ScaleType s) noexcept;

/** "C major", "F# minor", ... */
std::string keyName (int root, ScaleType s);

/** The in-key notes between `lowNote` and `highNote`, ascending. */
std::vector<int> notesInRange (int lowNote, int highNote, int root, ScaleType s);

inline constexpr int numPitchClasses = 12;

} // namespace sl
