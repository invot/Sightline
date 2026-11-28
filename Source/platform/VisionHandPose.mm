#include "VisionHandPose.h"

#import <CoreGraphics/CoreGraphics.h>
#import <Vision/Vision.h>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace sl
{
namespace
{
bool readPose (VNHumanHandPoseObservation* hand, int width, int height, HandPose& pose)
{
    // Use the same landmark order as the MediaPipe tracker and skeleton overlay.
    const VNHumanHandPoseObservationJointName joints[] {
        VNHumanHandPoseObservationJointNameWrist,
        VNHumanHandPoseObservationJointNameThumbCMC, VNHumanHandPoseObservationJointNameThumbMP,
        VNHumanHandPoseObservationJointNameThumbIP, VNHumanHandPoseObservationJointNameThumbTip,
        VNHumanHandPoseObservationJointNameIndexMCP, VNHumanHandPoseObservationJointNameIndexPIP,
        VNHumanHandPoseObservationJointNameIndexDIP, VNHumanHandPoseObservationJointNameIndexTip,
        VNHumanHandPoseObservationJointNameMiddleMCP, VNHumanHandPoseObservationJointNameMiddlePIP,
        VNHumanHandPoseObservationJointNameMiddleDIP, VNHumanHandPoseObservationJointNameMiddleTip,
        VNHumanHandPoseObservationJointNameRingMCP, VNHumanHandPoseObservationJointNameRingPIP,
        VNHumanHandPoseObservationJointNameRingDIP, VNHumanHandPoseObservationJointNameRingTip,
        VNHumanHandPoseObservationJointNameLittleMCP, VNHumanHandPoseObservationJointNameLittlePIP,
        VNHumanHandPoseObservationJointNameLittleDIP, VNHumanHandPoseObservationJointNameLittleTip
    };
    static_assert (std::size (joints) == HandPose::numPoints);

    auto* points = [hand recognizedPointsForJointsGroupName:VNHumanHandPoseObservationJointsGroupNameAll error:nullptr];
    pose.presence = 1.0f;
    for (int i = 0; i < HandPose::numPoints; ++i)
    {
        VNRecognizedPoint* point = points[joints[i]];
        if (point == nil || point.confidence < 0.15f)
            return false;

        pose.x[i] = (float) point.location.x * (float) width;
        pose.y[i] = (1.0f - (float) point.location.y) * (float) height;
        pose.presence = std::min (pose.presence, point.confidence);
    }

    // Vision supplies 2D points. Estimate scale from the palm so the shared
    // gesture thresholds stay roughly the same as the hand moves in depth.
    using P = HandPose;
    const float palmLength = std::hypot (pose.x[P::middleMcp] - pose.x[P::wrist],
                                         pose.y[P::middleMcp] - pose.y[P::wrist]);
    if (palmLength < 2.0f)
        return false;

    const float scale = 0.085f / palmLength;
    for (int i = 0; i < P::numPoints; ++i)
    {
        pose.wx[i] = (pose.x[i] - pose.x[P::wrist]) * scale;
        pose.wy[i] = (pose.y[i] - pose.y[P::wrist]) * scale;
    }
    pose.pinchDistance = std::hypot (pose.wx[P::thumbTip] - pose.wx[P::indexTip],
                                      pose.wy[P::thumbTip] - pose.wy[P::indexTip]);
    return true;
}

int detectHands (VNDetectHumanHandPoseRequest* request, const uint8_t* rgb,
                 int width, int height, int stride, std::array<HandPose, 2>& poses)
{
    @autoreleasepool
    {
        auto provider = CGDataProviderCreateWithData (nullptr, rgb, (size_t) stride * (size_t) height,
                                                     [] (void*, const void*, size_t) {});
        if (provider == nullptr)
            return 0;

        auto colourSpace = CGColorSpaceCreateDeviceRGB();
        auto image = CGImageCreate ((size_t) width, (size_t) height, 8, 24, (size_t) stride,
                                    colourSpace, (CGBitmapInfo) kCGImageAlphaNone, provider, nullptr,
                                    false, kCGRenderingIntentDefault);
        CGColorSpaceRelease (colourSpace);
        CGDataProviderRelease (provider);
        if (image == nullptr)
            return 0;

        auto* handler = [[VNImageRequestHandler alloc] initWithCGImage:image options:@{}];
        const bool detected = [handler performRequests:@[request] error:nullptr];
        CGImageRelease (image);
        if (! detected)
            return 0;

        int count = 0;
        for (VNHumanHandPoseObservation* hand in request.results)
        {
            if (count == (int) poses.size())
                break;

            HandPose pose;
            if (readPose (hand, width, height, pose))
                poses[(size_t) count++] = pose;
        }
        return count;
    }
}
}

std::unique_ptr<HandLandmarker> makeVisionHandLandmarker()
{
    if (@available (macOS 11.0, *))
    {
        auto* request = [[VNDetectHumanHandPoseRequest alloc] init];
        if (request == nil)
            return nullptr;

        request.maximumHandCount = 2;
        return std::make_unique<HandLandmarker> ([request] (const uint8_t* rgb, int width, int height,
                                                           int stride, std::array<HandPose, 2>& poses)
        {
            return detectHands (request, rgb, width, height, stride, poses);
        });
    }
    return nullptr;
}
}
