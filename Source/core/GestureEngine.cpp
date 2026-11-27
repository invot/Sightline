#include "GestureEngine.h"

#include <algorithm>

namespace sl
{

namespace
{
    // A little forgiveness at the edges of a target, so a hand resting on a
    // boundary does not flicker between two notes.
    constexpr float hitMarginX = 0.008f;
    constexpr float hitMarginY = 0.004f;
}

void GestureEngine::setZones (std::vector<Zone> newZones)
{
    zones = std::move (newZones);

    // Indices mean nothing after a rebuild.
    for (auto& s : sides)
    {
        s.active = -1;
        s.candidate = -1;
        s.activeNote = -1;
        s.activeChannel = 1;
        s.dwellStartMs = 0.0;
    }
}

void GestureEngine::setParams (int newVelocity, int newMelodyChannel, int newDrumChannel,
                               double newDwellMs, double newReleaseMs, bool drums)
{
    velocity = std::clamp (newVelocity, 1, 127);
    melodyChannel = std::clamp (newMelodyChannel, 1, 16);
    drumChannel = std::clamp (newDrumChannel, 1, 16);
    dwellMs = std::clamp (newDwellMs, 0.0, 500.0);
    releaseMs = std::clamp (newReleaseMs, 20.0, 800.0);
    drumsOverlay = drums;
}

int GestureEngine::channelFor (const Zone&) const
{
    return drumsOverlay ? drumChannel : melodyChannel;
}

void GestureEngine::reset (std::vector<NoteEvent>& out)
{
    for (int s = 0; s < 2; ++s)
    {
        if (sides[s].activeNote >= 0 && ! noteHeldElsewhere (s, sides[s].activeNote, sides[s].activeChannel))
            out.push_back (NoteEvent::off (sides[s].activeNote, sides[s].activeChannel));

        sides[s] = {};
    }
}

bool GestureEngine::noteHeldElsewhere (int side, int note, int channel) const
{
    const int other = side == 0 ? 1 : 0;
    return sides[other].activeNote == note && sides[other].activeChannel == channel;
}

int GestureEngine::hitTest (int side, float x, float y) const
{
    int best = -1;

    for (int i = 0; i < (int) zones.size(); ++i)
    {
        const auto& z = zones[(size_t) i];

        if (z.side != side)
            continue;

        const Rect grown = { z.bounds.x - hitMarginX, z.bounds.y - hitMarginY,
                             z.bounds.w + 2.0f * hitMarginX, z.bounds.h + 2.0f * hitMarginY };

        if (! grown.contains (x, y))
            continue;

        // The narrower target wins, so the short keys of the keyboard keep
        // their own slice of the rail.
        if (best < 0 || z.bounds.w < zones[(size_t) best].bounds.w)
            best = i;
    }

    return best;
}

float GestureEngine::dwellProgress (int side, double nowMs) const noexcept
{
    const auto& s = sides[side & 1];

    if (s.hovering)
        return 0.0f;

    if (s.candidate < 0 || s.candidate == s.active)
        return s.active >= 0 ? 1.0f : 0.0f;

    if (dwellMs <= 0.0)
        return 1.0f;

    return (float) std::clamp ((nowMs - s.dwellStartMs) / dwellMs, 0.0, 1.0);
}

void GestureEngine::release (int side, std::vector<NoteEvent>& out)
{
    auto& s = sides[side];

    if (s.activeNote >= 0 && ! noteHeldElsewhere (side, s.activeNote, s.activeChannel))
        out.push_back (NoteEvent::off (s.activeNote, s.activeChannel));

    s.active = -1;
    s.activeNote = -1;
}

void GestureEngine::start (int side, int zone, std::vector<NoteEvent>& out)
{
    release (side, out);

    auto& state = sides[side];
    const auto& z = zones[(size_t) zone];
    const int ch = channelFor (z);

    // Releasing the other side may already have released this exact
    // note; don't send a second note-off for it later.
    out.push_back (NoteEvent::on (z.note, velocity, ch));

    state.active = zone;
    state.activeNote = z.note;
    state.activeChannel = ch;
}

void GestureEngine::update (const HandSample& left, const HandSample& right, double nowMs,
                            std::vector<NoteEvent>& out)
{
    const HandSample* hands[2] = { &left, &right };

    for (int s = 0; s < 2; ++s)
    {
        const auto& hand = *hands[s];
        auto& state = sides[s];

        const int hit = hand.detected ? hitTest (s, hand.x, hand.y) : -1;
        const bool gated = pinchToPlay && hand.detected && hand.hasPinch;
        state.hovering = gated && ! hand.pinching;

        if (gated && ! hand.pinching)
        {
            // An open hand only points: show the target, and stop at once
            // whatever the last pinch was playing.
            state.pinchHeld = false;
            state.candidate = hit;
            state.dwellStartMs = nowMs;

            if (state.activeNote >= 0)
                release (s, out);

            continue;
        }

        if (hit < 0)
        {
            state.candidate = -1;

            if (! hand.detected)
                state.pinchHeld = false;

            if (state.activeNote >= 0 && nowMs - state.lastSeenMs >= releaseMs)
                release (s, out);

            continue;
        }

        state.lastSeenMs = nowMs;

        if (gated && ! state.pinchHeld)
        {
            // The pinch closing is the trigger: no dwell.
            state.pinchHeld = true;
            state.candidate = hit;
            state.dwellStartMs = nowMs;

            if (hit != state.active)
                start (s, hit, out);

            continue;
        }

        if (hit != state.candidate)
        {
            state.candidate = hit;
            state.dwellStartMs = nowMs;
        }

        if (hit == state.active)
            continue;

        if (nowMs - state.dwellStartMs >= dwellMs)
            start (s, hit, out);
    }
}

} // namespace sl
