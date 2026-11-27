#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace sl
{

/** One neural network with a single NHWC float input. Implemented over ONNX
    Runtime in OrtModel; the tests can plug in a fake. */
class InferenceModel
{
public:
    virtual ~InferenceModel() = default;

    /** input holds the whole input tensor. Each output is resized and filled in
        the model's output order. Returns false if inference failed. */
    virtual bool run (const float* input, std::vector<std::vector<float>>& outputs) = 0;
};

/** One hand found by the landmark model. Image coordinates are frame pixels. */
struct HandPose
{
    static constexpr int numPoints = 21;

    enum Point
    {
        wrist = 0, thumbTip = 4, indexMcp = 5, indexTip = 8, middleMcp = 9, pinkyMcp = 17
    };

    float x[numPoints] {}, y[numPoints] {};   // frame pixels
    float wx[numPoints] {}, wy[numPoints] {}, wz[numPoints] {};   // metres, hand-centred
    float presence = 0.0f;
    float handedness = 0.0f;   // 0 = left hand, 1 = right hand (as the camera sees it)

    /** Smoothed point between the thumb tip and index tip, in frame pixels: the
        "cursor" a pinch closes on. */
    float cursorX = 0.0f, cursorY = 0.0f;
    /** Thumb tip to index tip, metres. */
    float pinchDistance = 1.0f;
    bool pinching = false;
    int id = 0;   // stays the same while the hand is tracked
};

/** MediaPipe-style two-stage hand tracker: a palm detector finds new hands, a
    landmark model finds 21 points on each, and the next frame's crop is taken
    from those points so the detector only runs while a hand is missing.

    The models are the MediaPipe palm detector (192x192) and hand landmark
    model (224x224) as published in the OpenCV model zoo. */
class HandLandmarker
{
public:
    HandLandmarker (std::unique_ptr<InferenceModel> palmModel,
                    std::unique_ptr<InferenceModel> landmarkModel);

    void reset();

    /** rgb: packed RGB, top-down. nowMs drives the cursor smoothing. Returns
        the number of hands found (0..2). */
    int process (const uint8_t* rgb, int width, int height, int rowStrideBytes, double nowMs);

    const std::vector<HandPose>& hands() const noexcept { return found; }

    struct Palm
    {
        float cx, cy, w, h;     // frame pixels
        float kx[7], ky[7];     // palm keypoints
        float score;
    };

    /** Exposed for the tests. */
    std::vector<Palm> detectPalms (const uint8_t* rgb, int width, int height, int rowStrideBytes);

    /** Pinch hysteresis, metres between the thumb and index tips. */
    static constexpr float pinchOnMetres = 0.032f;
    static constexpr float pinchOffMetres = 0.050f;

private:
    struct Roi
    {
        float cx = 0, cy = 0, size = 0, angle = 0;   // square, rotated
    };

    struct OneEuro
    {
        bool primed = false;
        float value = 0.0f, deriv = 0.0f;
        float filter (float x, float dt, float minCutoff, float beta);
    };

    struct Slot
    {
        bool active = false;
        Roi roi;
        HandPose pose;
        OneEuro fx, fy;
        double lastMs = 0.0;
    };

    bool runLandmarks (const uint8_t* rgb, int width, int height, int stride,
                       const Roi&, HandPose& out);
    static Roi roiFromPalm (const Palm&);
    static Roi roiFromPose (const HandPose&);
    void updateCursor (Slot&, int width, int height, double nowMs, bool fresh);

    std::unique_ptr<InferenceModel> palm, landmark;
    std::vector<float> palmInput, handInput;
    std::vector<std::vector<float>> outputs;
    std::vector<float> anchors;   // x, y pairs
    Slot slots[2];
    std::vector<HandPose> found;
    int nextId = 1;
};

/** Samples a rotated square of the frame into an NHWC float tensor (0..1),
    black outside the frame. Public for the tests. */
void warpToTensor (const uint8_t* rgb, int width, int height, int rowStrideBytes,
                   float centreX, float centreY, float size, float angle,
                   int outSize, float* dst);

} // namespace sl
