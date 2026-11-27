#include "Scales.h"

namespace sl
{

namespace
{
    constexpr uint16_t maskOf (std::initializer_list<int> pcs)
    {
        uint16_t m = 0;

        for (int pc : pcs)
            m = (uint16_t) (m | (1u << pc));

        return m;
    }
}

const char* scaleName (ScaleType s) noexcept
{
    switch (s)
    {
        case ScaleType::Chromatic:       return "Chromatic";
        case ScaleType::Major:           return "Major";
        case ScaleType::Minor:           return "Minor";
        case ScaleType::Dorian:          return "Dorian";
        case ScaleType::Mixolydian:      return "Mixolydian";
        case ScaleType::PentatonicMajor: return "Pentatonic Major";
        case ScaleType::PentatonicMinor: return "Pentatonic Minor";
        case ScaleType::Blues:           return "Blues";
        case ScaleType::NumScales:       break;
    }

    return "Major";
}

const char* overlayName (Overlay o) noexcept
{
    switch (o)
    {
        case Overlay::Keys:  return "Keys";
        case Overlay::Scale: return "Scale";
        case Overlay::Drums: return "Drums";
    }

    return "Keys";
}

uint16_t scaleMask (ScaleType s) noexcept
{
    switch (s)
    {
        case ScaleType::Chromatic:       return 0x0fff;
        case ScaleType::Major:           return maskOf ({ 0, 2, 4, 5, 7, 9, 11 });
        case ScaleType::Minor:           return maskOf ({ 0, 2, 3, 5, 7, 8, 10 });
        case ScaleType::Dorian:          return maskOf ({ 0, 2, 3, 5, 7, 9, 10 });
        case ScaleType::Mixolydian:      return maskOf ({ 0, 2, 4, 5, 7, 9, 10 });
        case ScaleType::PentatonicMajor: return maskOf ({ 0, 2, 4, 7, 9 });
        case ScaleType::PentatonicMinor: return maskOf ({ 0, 3, 5, 7, 10 });
        case ScaleType::Blues:           return maskOf ({ 0, 3, 5, 6, 7, 10 });
        case ScaleType::NumScales:       break;
    }

    return 0x0fff;
}

bool noteInScale (int note, int root, ScaleType s) noexcept
{
    const int pc = ((note - root) % 12 + 12) % 12;
    return (scaleMask (s) & (1u << pc)) != 0;
}

static const char* noteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

const char* pitchClassName (int pc) noexcept
{
    return noteNames[((pc % 12) + 12) % 12];
}

int scaleDegree (int note, int root, ScaleType s) noexcept
{
    const int pc = ((note - root) % 12 + 12) % 12;
    const auto mask = scaleMask (s);

    if ((mask & (1u << pc)) == 0)
        return 0;

    int degree = 1;

    for (int i = 0; i < pc; ++i)
        if ((mask & (1u << i)) != 0)
            ++degree;

    return degree;
}

std::string midiNoteName (int note)
{
    const int pc = ((note % 12) + 12) % 12;
    const int octave = note / 12 - 1;
    return std::string (noteNames[pc]) + std::to_string (octave);
}

std::string keyName (int root, ScaleType s)
{
    const int pc = ((root % 12) + 12) % 12;
    return std::string (noteNames[pc]) + " " + scaleName (s);
}

std::vector<int> notesInRange (int lowNote, int highNote, int root, ScaleType s)
{
    std::vector<int> notes;

    for (int n = lowNote; n <= highNote; ++n)
        if (noteInScale (n, root, s))
            notes.push_back (n);

    return notes;
}

} // namespace sl
