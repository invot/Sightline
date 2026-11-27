#pragma once

#include "HandLandmarker.h"
#include "Types.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace sl
{

/** Finds the hand on each side of the frame.

    With a HandLandmarker attached (the normal case), hands come from the
    MediaPipe palm detector and landmark model: 21 points per hand, the cursor
    sits between the thumb and index fingertips, and a pinch is reported. No
    hand in the picture means no hand reported.

    Without one (ONNX Runtime missing), it falls back to the old heuristic:
    skin tone and motion inside the two side regions where the targets live.
    That needs no model but is easily fooled by warm walls and sensor noise. */
class HandTracker
{
public:
    void prepare (int frameWidth, int frameHeight);
    void reset();

    /** Switches to the landmark model (nullptr goes back to the heuristic). */
    void setLandmarker (std::unique_ptr<HandLandmarker>);
    bool usesModel() const noexcept { return landmarker != nullptr; }

    /** Pinch point (default) or pointing fingertip. With Fingertip, a hand that
        isn't pointing is not reported at all, so it can't select anything. */
    void setAimMode (AimMode m) noexcept { aim = m; }
    AimMode aimMode() const noexcept { return aim; }

    /** rgb: tightly packed RGB pixels, top-down. sensitivity 0..1 widens the
        skin ranges and lowers the motion threshold (heuristic only). nowMs
        drives the model's cursor smoothing; < 0 assumes 30 fps. */
    void analyse (const uint8_t* rgb, int frameWidth, int frameHeight, int rowStrideBytes,
                  float sensitivity01, double nowMs = -1.0);

    /** 0 = left half of the frame, 1 = right half. */
    const HandSample& side (int index) const noexcept { return hands[index & 1]; }

    /** The landmarks behind side(index), in 0..1 frame coordinates, or nullptr
        when that side has no hand or the heuristic is in use. */
    const HandPose* pose (int index) const noexcept
    {
        return hands[index & 1].detected && usesModel() ? &poses[index & 1] : nullptr;
    }

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
    void analyseHeuristic (const uint8_t* rgb, int frameWidth, int frameHeight, int rowStrideBytes,
                           float sensitivity01);
    void analyseModel (const uint8_t* rgb, int frameWidth, int frameHeight, int rowStrideBytes,
                       double nowMs);

    std::unique_ptr<HandLandmarker> landmarker;
    HandPose poses[2];
    double clockMs = 0.0;
    AimMode aim = AimMode::PinchPoint;

    HandSample hands[2];
    Track tracks[2];
    std::vector<uint8_t> luma, prevLuma, mask;
    int gridW = 0, gridH = 0;
    int frameW = 0, frameH = 0;
    bool havePrev = false;
};

} // namespace sl
