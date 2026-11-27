#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sl
{

/** Minimal geometry so the core stays JUCE-free and testable from a console. */
struct Rect
{
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;

    float getRight()  const noexcept { return x + w; }
    float getBottom() const noexcept { return y + h; }
    float getCentreX() const noexcept { return x + w * 0.5f; }
    float getCentreY() const noexcept { return y + h * 0.5f; }

    bool contains (float px, float py) const noexcept
    {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }

    bool intersects (const Rect& o) const noexcept
    {
        return x < o.getRight() && o.x < getRight() && y < o.getBottom() && o.y < getBottom();
    }

    Rect inflated (float m) const noexcept { return { x - m, y - m, w + 2.0f * m, h + 2.0f * m }; }
};

/** Which set of targets is drawn on the two side rails. */
enum class Overlay
{
    Keys = 0,   // classic chromatic keyboard, one octave per side
    Scale = 1,  // only the notes of the selected key
    Drums = 2   // a drum kit, one pad per sound
};

constexpr int numOverlays = 3;

const char* overlayName (Overlay o) noexcept;

/** MIDI note -> "C4", "F#3", ... */
std::string midiNoteName (int note);

struct Zone
{
    Rect bounds;
    int note = 60;
    int side = 0;          // 0 = left rail, 1 = right rail
    bool blackKey = false;
    std::string label;
    std::string subLabel;
};

struct HandSample
{
    bool detected = false;
    float x = 0.0f;        // normalised display coords (already mirrored)
    float y = 0.0f;
    float size = 0.0f;     // blob area as a fraction of the frame
    float confidence = 0.0f;
};

/** One MIDI message, queued from the gesture engine to the audio thread. */
struct NoteEvent
{
    enum Type : uint8_t { noteOn = 1, noteOff = 2, allNotesOff = 3 };

    uint8_t type = noteOff;
    uint8_t note = 0;
    uint8_t velocity = 0;
    uint8_t channel = 1;   // 1-16

    static NoteEvent on (int note, int vel, int ch)
    {
        return { noteOn, (uint8_t) note, (uint8_t) vel, (uint8_t) ch };
    }

    static NoteEvent off (int note, int ch)
    {
        return { noteOff, (uint8_t) note, 0, (uint8_t) ch };
    }

    static NoteEvent panic()
    {
        return { allNotesOff, 0, 0, 1 };
    }
};

} // namespace sl
