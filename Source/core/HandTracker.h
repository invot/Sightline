#pragma once

#include "Types.h"

#include <cstdint>
#include <vector>

namespace sl
{

/** Finds the hand on each side of the frame.

    There is no model to ship: hands are found from skin tone and motion inside
    the two side regions where the targets live. The face sits in the middle, so
    it is out of the search area, and a resting hand is still found because the
    skin mask does not need movement. A moving, non-skin object (a sleeve, a
    glove in poor light) falls back to motion alone. */
class HandTracker
{
public:
    void prepare (int frameWidth, int frameHeight);
    void reset();

    /** rgb: tightly packed RGB pixels, top-down. sensitivity 0..1 widens the
        skin ranges and lowers the motion threshold. */
    void analyse (const uint8_t* rgb, int frameWidth, int frameHeight, int rowStrideBytes,
                  float sensitivity01);

    /** 0 = left half of the frame, 1 = right half. */
    const HandSample& side (int index) const noexcept { return hands[index & 1]; }

    /** Debug mask of the last analysed frame (0..1 per cell), for the UI's
        "show tracking" overlay. Empty until the first analyse(). */
    int maskWidth() const noexcept { return gridW; }
    int maskHeight() const noexcept { return gridH; }
    const std::vector<uint8_t>& lastMask() const noexcept { return mask; }

private:
    struct Track
    {
        bool valid = false;
        float x = 0.0f, y = 0.0f, size = 0.0f;
        int missed = 0;
    };

    void resetTrack (Track&);

    HandSample hands[2];
    Track tracks[2];
    std::vector<uint8_t> luma, prevLuma, mask;
    int gridW = 0, gridH = 0;
    int frameW = 0, frameH = 0;
    bool havePrev = false;
};

} // namespace sl
