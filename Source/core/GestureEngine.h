#pragma once

#include "Types.h"

#include <vector>

namespace sl
{

/** Turns "hand is here" into note on/off.

    A note starts once the hand has rested on a target for the dwell time, so a
    hand crossing the rail on its way somewhere else does not play a glissando.
    Leaving the target, or losing the hand, ends the note after a short release
    window. Each side is independent, so both hands can hold notes at once. */
class GestureEngine
{
public:
    void setZones (std::vector<Zone> newZones);
    const std::vector<Zone>& getZones() const noexcept { return zones; }

    void setParams (int velocity, int melodyChannel, int drumChannel,
                    double dwellMs, double releaseMs, bool drumsOverlay);

    /** Releases anything sounding (parameter changes, editor closing). */
    void reset (std::vector<NoteEvent>& out);

    void update (const HandSample& left, const HandSample& right, double nowMs,
                 std::vector<NoteEvent>& out);

    /** Zone index under each hand's last position, for the UI highlight. */
    int hoveredZone (int side) const noexcept { return sides[side & 1].candidate; }
    int activeZone (int side) const noexcept  { return sides[side & 1].active; }
    /** 0..1 while the hand rests on a target and the dwell time runs down. */
    float dwellProgress (int side, double nowMs) const noexcept;

private:
    struct SideState
    {
        int active = -1;
        int candidate = -1;
        int activeNote = -1;
        int activeChannel = 1;
        double dwellStartMs = 0.0;
        double lastSeenMs = -1.0;
    };

    int hitTest (int side, float x, float y) const;
    void release (int side, std::vector<NoteEvent>& out);
    bool noteHeldElsewhere (int side, int note, int channel) const;
    int channelFor (const Zone& z) const;

    std::vector<Zone> zones;
    SideState sides[2];

    int velocity = 100;
    int melodyChannel = 1;
    int drumChannel = 10;
    double dwellMs = 90.0;
    double releaseMs = 140.0;
    bool drumsOverlay = false;
};

} // namespace sl
