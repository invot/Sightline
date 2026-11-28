#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
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
        wrist = 0, thumbTip = 4, indexMcp = 5, indexPip = 6, indexDip = 7, indexTip = 8,
        middleMcp = 9, middlePip = 10, middleTip = 12,
        ringPip = 14, ringTip = 16, pinkyMcp = 17, pinkyPip = 18, pinkyTip = 20
    };

    float x[numPoints] {}, y[numPoints] {};   // frame pixels
    float wx[numPoints] {}, wy[numPoints] {}, wz[numPoints] {};   // metres, hand-centred
    float presence = 0.0f;
    float handedness = 0.0f;   // 0 = left hand, 1 = right hand (as the camera sees it)

    /** Smoothed point between the thumb tip and index tip, in frame pixels: the
        "cursor" a pinch closes on. */
    float cursorX = 0.0f, cursorY = 0.0f;
    /** Smoothed index fingertip, frame pixels: the cursor while pointing. */
    float tipX = 0.0f, tipY = 0.0f;
    /** Thumb tip to index tip, metres. */
    float pinchDistance = 1.0f;
    bool pinching = false;
    /** Index finger out, the other three curled. */
    bool pointing = false;
    /** While pointing: thumb tip pressed against the side of the index finger,
        like pulling a trigger. Metres from the thumb tip to that finger. */
    float clickDistance = 1.0f;
    bool clicking = false;
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
    using PoseDetector = std::function<int (const uint8_t*, int, int, int, std::array<HandPose, 2>&)>;

    HandLandmarker (std::unique_ptr<InferenceModel> palmModel,
                    std::unique_ptr<InferenceModel> landmarkModel);
    explicit HandLandmarker (PoseDetector);

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
    static constexpr float clickOnMetres = 0.022f;
    static constexpr float clickOffMetres = 0.035f;

    /** The finger tests on world landmarks, exposed for the tests. */
    static bool looksLikePointing (const HandPose&);
    static float thumbToIndexSide (const HandPose&);

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
        OneEuro fx, fy, tx, ty;
        double lastMs = 0.0;
        int pointVotes = 0;       // debounces the pointing pose
        bool clickArmed = false;  // the thumb has been seen open since pointing began
    };

    bool runLandmarks (const uint8_t* rgb, int width, int height, int stride,
                       const Roi&, HandPose& out);
    static Roi roiFromPalm (const Palm&);
    static Roi roiFromPose (const HandPose&);
    void updateCursor (Slot&, int width, int height, double nowMs, bool fresh);
    void trackDetectedPoses (const uint8_t* rgb, int width, int height, int stride, double nowMs);

    PoseDetector detectPoses;
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
