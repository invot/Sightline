#include "HandLandmarker.h"

#include <algorithm>
#include <cmath>

namespace sl
{

namespace
{
    constexpr int palmSize = 192;
    constexpr int handSize = 224;
    constexpr int numAnchors = 2016;
    constexpr float pi = 3.14159265358979f;

    constexpr float palmScoreThreshold = 0.5f;
    constexpr float palmNmsIou = 0.3f;
    constexpr float newHandPresence = 0.7f;    // a fresh detection has to be convincing
    constexpr float keepHandPresence = 0.5f;   // a tracked hand is dropped below this

    float normaliseAngle (float a)
    {
        return a - 2.0f * pi * std::floor ((a + pi) / (2.0f * pi));
    }

    /** Rotation that turns the vector from p0 to p1 to point straight up. */
    float uprightAngle (float x0, float y0, float x1, float y1)
    {
        return normaliseAngle (pi * 0.5f - std::atan2 (-(y1 - y0), x1 - x0));
    }

    float sigmoid (float x) { return 1.0f / (1.0f + std::exp (-x)); }

    struct Box { float x0, y0, x1, y1; };

    float iou (const Box& a, const Box& b)
    {
        const float ix = std::max (0.0f, std::min (a.x1, b.x1) - std::max (a.x0, b.x0));
        const float iy = std::max (0.0f, std::min (a.y1, b.y1) - std::max (a.y0, b.y0));
        const float inter = ix * iy;
        const float uni = (a.x1 - a.x0) * (a.y1 - a.y0) + (b.x1 - b.x0) * (b.y1 - b.y0) - inter;
        return uni > 0.0f ? inter / uni : 0.0f;
    }

    Box boundsOf (const HandPose& p)
    {
        Box b { p.x[0], p.y[0], p.x[0], p.y[0] };

        for (int i = 1; i < HandPose::numPoints; ++i)
        {
            b.x0 = std::min (b.x0, p.x[i]);  b.x1 = std::max (b.x1, p.x[i]);
            b.y0 = std::min (b.y0, p.y[i]);  b.y1 = std::max (b.y1, p.y[i]);
        }

        return b;
    }
}

//==============================================================================
void warpToTensor (const uint8_t* rgb, int width, int height, int stride,
                   float centreX, float centreY, float size, float angle,
                   int outSize, float* dst)
{
    const float c = std::cos (angle), s = std::sin (angle);
    const float k = size / (float) outSize;   // source pixels per output pixel

    // Bilinear sampling aliases badly when shrinking a lot (a 640 px frame into
    // the 192 px palm input), so average a few samples per output pixel.
    const int ss = std::clamp ((int) std::ceil (k * 0.75f), 1, 4);
    const float inv = 1.0f / (255.0f * (float) (ss * ss));
    const float half = (float) outSize * 0.5f;

    for (int v = 0; v < outSize; ++v)
    {
        for (int u = 0; u < outSize; ++u)
        {
            float acc[3] { 0.0f, 0.0f, 0.0f };

            for (int j = 0; j < ss; ++j)
            {
                const float b = ((float) v + ((float) j + 0.5f) / (float) ss - half) * k;

                for (int i = 0; i < ss; ++i)
                {
                    const float a = ((float) u + ((float) i + 0.5f) / (float) ss - half) * k;

                    // Pixel centres sit at +0.5.
                    const float sx = centreX + a * c - b * s - 0.5f;
                    const float sy = centreY + a * s + b * c - 0.5f;

                    const int x0 = (int) std::floor (sx), y0 = (int) std::floor (sy);
                    const float fx = sx - (float) x0, fy = sy - (float) y0;

                    for (int dy = 0; dy < 2; ++dy)
                    {
                        const int yy = y0 + dy;

                        if (yy < 0 || yy >= height)
                            continue;

                        const float wy = dy == 0 ? 1.0f - fy : fy;
                        const uint8_t* row = rgb + (size_t) yy * (size_t) stride;

                        for (int dx = 0; dx < 2; ++dx)
                        {
                            const int xx = x0 + dx;

                            if (xx < 0 || xx >= width)
                                continue;

                            const float w = wy * (dx == 0 ? 1.0f - fx : fx);
                            const uint8_t* p = row + (size_t) xx * 3u;
                            acc[0] += w * (float) p[0];
                            acc[1] += w * (float) p[1];
                            acc[2] += w * (float) p[2];
                        }
                    }
                }
            }

            float* out = dst + ((size_t) v * (size_t) outSize + (size_t) u) * 3u;
            out[0] = acc[0] * inv;
            out[1] = acc[1] * inv;
            out[2] = acc[2] * inv;
        }
    }
}

//==============================================================================
float HandLandmarker::OneEuro::filter (float x, float dt, float minCutoff, float beta)
{
    if (! primed || dt <= 0.0f)
    {
        primed = true;
        value = x;
        deriv = 0.0f;
        return value;
    }

    const auto alpha = [dt] (float cutoff)
    {
        const float tau = 1.0f / (2.0f * pi * cutoff);
        return 1.0f / (1.0f + tau / dt);
    };

    const float d = (x - value) / dt;
    deriv += alpha (1.0f) * (d - deriv);
    value += alpha (minCutoff + beta * std::abs (deriv)) * (x - value);
    return value;
}

//==============================================================================
HandLandmarker::HandLandmarker (std::unique_ptr<InferenceModel> palmModel,
                                std::unique_ptr<InferenceModel> landmarkModel)
    : palm (std::move (palmModel)), landmark (std::move (landmarkModel))
{
    palmInput.resize ((size_t) palmSize * palmSize * 3u);
    handInput.resize ((size_t) handSize * handSize * 3u);

    // SSD anchors of the MediaPipe palm detector: a 24x24 grid with two
    // anchors a cell, then a 12x12 grid with six.
    anchors.reserve ((size_t) numAnchors * 2u);

    for (auto [stride, perCell] : { std::pair { 8, 2 }, std::pair { 16, 6 } })
    {
        const int grid = palmSize / stride;

        for (int y = 0; y < grid; ++y)
            for (int x = 0; x < grid; ++x)
                for (int n = 0; n < perCell; ++n)
                {
                    anchors.push_back (((float) x + 0.5f) / (float) grid);
                    anchors.push_back (((float) y + 0.5f) / (float) grid);
                }
    }
}

void HandLandmarker::reset()
{
    for (auto& s : slots)
        s = {};

    found.clear();
}

std::vector<HandLandmarker::Palm> HandLandmarker::detectPalms (const uint8_t* rgb, int width, int height,
                                                               int stride)
{
    std::vector<Palm> result;

    // Letterbox the whole frame into the square input.
    const float side = (float) std::max (width, height);
    const float cx = (float) width * 0.5f, cy = (float) height * 0.5f;
    warpToTensor (rgb, width, height, stride, cx, cy, side, 0.0f, palmSize, palmInput.data());

    if (! palm->run (palmInput.data(), outputs) || outputs.size() < 2
        || outputs[0].size() < (size_t) numAnchors * 18u || outputs[1].size() < (size_t) numAnchors)
        return result;

    const auto& reg = outputs[0];
    const auto& cls = outputs[1];

    std::vector<std::pair<float, int>> candidates;

    for (int i = 0; i < numAnchors; ++i)
    {
        const float score = sigmoid (cls[(size_t) i]);

        if (score >= palmScoreThreshold)
            candidates.push_back ({ score, i });
    }

    std::sort (candidates.begin(), candidates.end(), [] (auto& a, auto& b) { return a.first > b.first; });

    const auto toFrameX = [&] (float v, int i) { return (v / palmSize + anchors[(size_t) i * 2u] - 0.5f) * side + cx; };
    const auto toFrameY = [&] (float v, int i) { return (v / palmSize + anchors[(size_t) i * 2u + 1u] - 0.5f) * side + cy; };

    for (auto [score, i] : candidates)
    {
        const float* r = reg.data() + (size_t) i * 18u;

        Palm p;
        p.cx = toFrameX (r[0], i);
        p.cy = toFrameY (r[1], i);
        p.w = r[2] / palmSize * side;
        p.h = r[3] / palmSize * side;
        p.score = score;

        for (int k = 0; k < 7; ++k)
        {
            p.kx[k] = toFrameX (r[4 + 2 * k], i);
            p.ky[k] = toFrameY (r[5 + 2 * k], i);
        }

        const Box b { p.cx - p.w * 0.5f, p.cy - p.h * 0.5f, p.cx + p.w * 0.5f, p.cy + p.h * 0.5f };
        bool overlaps = false;

        for (const auto& q : result)
        {
            const Box qb { q.cx - q.w * 0.5f, q.cy - q.h * 0.5f, q.cx + q.w * 0.5f, q.cy + q.h * 0.5f };

            if (iou (b, qb) > palmNmsIou)
            {
                overlaps = true;
                break;
            }
        }

        if (! overlaps && p.w > 0.0f && p.h > 0.0f)
            result.push_back (p);

        if (result.size() >= 4)
            break;
    }

    return result;
}

HandLandmarker::Roi HandLandmarker::roiFromPalm (const Palm& p)
{
    // Wrist (0) to middle-finger knuckle (2) sets the rotation; the box grows
    // to take in the fingers and shifts half a palm towards them.
    Roi r;
    r.angle = uprightAngle (p.kx[0], p.ky[0], p.kx[2], p.ky[2]);
    r.size = std::max (p.w, p.h) * 2.6f;

    const float shift = -0.5f * p.h;
    r.cx = p.cx - std::sin (r.angle) * shift;
    r.cy = p.cy + std::cos (r.angle) * shift;
    return r;
}

HandLandmarker::Roi HandLandmarker::roiFromPose (const HandPose& p)
{
    // As MediaPipe: the box around the palm and the lower finger joints,
    // measured upright, doubled, nudged a little towards the fingers.
    static constexpr int subset[] { 0, 1, 2, 3, 5, 6, 9, 10, 13, 14, 17, 18 };

    Roi r;
    r.angle = uprightAngle (p.x[HandPose::wrist], p.y[HandPose::wrist],
                            p.x[HandPose::middleMcp], p.y[HandPose::middleMcp]);

    const float c = std::cos (r.angle), s = std::sin (r.angle);
    float a0 = 1e9f, a1 = -1e9f, b0 = 1e9f, b1 = -1e9f;

    for (int i : subset)
    {
        const float a = p.x[i] * c + p.y[i] * s;
        const float b = -p.x[i] * s + p.y[i] * c;
        a0 = std::min (a0, a);  a1 = std::max (a1, a);
        b0 = std::min (b0, b);  b1 = std::max (b1, b);
    }

    const float ac = (a0 + a1) * 0.5f;
    const float bc = (b0 + b1) * 0.5f - 0.1f * (b1 - b0);

    r.cx = ac * c - bc * s;
    r.cy = ac * s + bc * c;
    r.size = std::max (a1 - a0, b1 - b0) * 2.0f;
    return r;
}

bool HandLandmarker::runLandmarks (const uint8_t* rgb, int width, int height, int stride,
                                   const Roi& roi, HandPose& out)
{
    if (roi.size < 8.0f)
        return false;

    warpToTensor (rgb, width, height, stride, roi.cx, roi.cy, roi.size, roi.angle, handSize, handInput.data());

    if (! landmark->run (handInput.data(), outputs) || outputs.size() < 4
        || outputs[0].size() < 63u || outputs[1].empty() || outputs[2].empty() || outputs[3].size() < 63u)
        return false;

    out.presence = outputs[1][0];
    out.handedness = outputs[2][0];

    const float c = std::cos (roi.angle), s = std::sin (roi.angle);

    for (int i = 0; i < HandPose::numPoints; ++i)
    {
        const float a = (outputs[0][(size_t) i * 3u] / (float) handSize - 0.5f) * roi.size;
        const float b = (outputs[0][(size_t) i * 3u + 1u] / (float) handSize - 0.5f) * roi.size;

        out.x[i] = roi.cx + a * c - b * s;
        out.y[i] = roi.cy + a * s + b * c;
        out.wx[i] = outputs[3][(size_t) i * 3u];
        out.wy[i] = outputs[3][(size_t) i * 3u + 1u];
        out.wz[i] = outputs[3][(size_t) i * 3u + 2u];
    }

    const float dx = out.wx[HandPose::thumbTip] - out.wx[HandPose::indexTip];
    const float dy = out.wy[HandPose::thumbTip] - out.wy[HandPose::indexTip];
    const float dz = out.wz[HandPose::thumbTip] - out.wz[HandPose::indexTip];
    out.pinchDistance = std::sqrt (dx * dx + dy * dy + dz * dz);
    return true;
}

namespace
{
    float dist3 (const HandPose& p, int a, int b)
    {
        const float dx = p.wx[a] - p.wx[b], dy = p.wy[a] - p.wy[b], dz = p.wz[a] - p.wz[b];
        return std::sqrt (dx * dx + dy * dy + dz * dz);
    }

    /** Distance from point i to the segment a-b, world metres. */
    float toSegment (const HandPose& p, int i, int a, int b)
    {
        const float ax = p.wx[b] - p.wx[a], ay = p.wy[b] - p.wy[a], az = p.wz[b] - p.wz[a];
        const float px = p.wx[i] - p.wx[a], py = p.wy[i] - p.wy[a], pz = p.wz[i] - p.wz[a];
        const float len2 = ax * ax + ay * ay + az * az;
        const float t = len2 > 0.0f ? std::clamp ((px * ax + py * ay + pz * az) / len2, 0.0f, 1.0f) : 0.0f;
        const float dx = px - t * ax, dy = py - t * ay, dz = pz - t * az;
        return std::sqrt (dx * dx + dy * dy + dz * dz);
    }

    /** Cosine of the bend at joint b along a-b-c: 1 = dead straight. */
    float straightness (const HandPose& p, int a, int b, int c)
    {
        const float ux = p.wx[b] - p.wx[a], uy = p.wy[b] - p.wy[a], uz = p.wz[b] - p.wz[a];
        const float vx = p.wx[c] - p.wx[b], vy = p.wy[c] - p.wy[b], vz = p.wz[c] - p.wz[b];
        const float n = std::sqrt ((ux * ux + uy * uy + uz * uz) * (vx * vx + vy * vy + vz * vz));
        return n > 0.0f ? (ux * vx + uy * vy + uz * vz) / n : 0.0f;
    }
}

bool HandLandmarker::looksLikePointing (const HandPose& p)
{
    using P = HandPose;

    // The index finger is straight and reaches past its middle joint...
    const bool indexOut = straightness (p, P::indexMcp, P::indexPip, P::indexTip) > 0.75f
                       && dist3 (p, P::wrist, P::indexTip) > dist3 (p, P::wrist, P::indexPip) * 1.15f;

    if (! indexOut)
        return false;

    // ...and at least two of the other three fold back towards the palm.
    int curled = 0;
    curled += dist3 (p, P::wrist, P::middleTip) < dist3 (p, P::wrist, P::middlePip) * 1.05f ? 1 : 0;
    curled += dist3 (p, P::wrist, P::ringTip)   < dist3 (p, P::wrist, P::ringPip)   * 1.05f ? 1 : 0;
    curled += dist3 (p, P::wrist, P::pinkyTip)  < dist3 (p, P::wrist, P::pinkyPip)  * 1.05f ? 1 : 0;
    return curled >= 2;
}

float HandLandmarker::thumbToIndexSide (const HandPose& p)
{
    using P = HandPose;
    return std::min (toSegment (p, P::thumbTip, P::indexMcp, P::indexPip),
                     toSegment (p, P::thumbTip, P::indexPip, P::indexDip));
}

void HandLandmarker::updateCursor (Slot& slot, int width, int height, double nowMs, bool fresh)
{
    auto& p = slot.pose;

    if (fresh)
    {
        slot.fx = {};
        slot.fy = {};
        slot.tx = {};
        slot.ty = {};
        slot.pointVotes = 0;
        slot.clickArmed = false;
        p.pinching = p.pointing = p.clicking = false;
    }

    // Hysteresis, so a pinch held at the threshold doesn't chatter.
    p.pinching = p.pinching ? p.pinchDistance < pinchOffMetres
                            : p.pinchDistance < pinchOnMetres;

    // Pointing has to hold for two frames to start or stop, so one odd
    // landmark frame doesn't flash the cursor on and off.
    const bool pointNow = looksLikePointing (p);
    slot.pointVotes = std::clamp (slot.pointVotes + (pointNow ? 1 : -1), 0, 2);

    if (slot.pointVotes == 2)       p.pointing = true;
    else if (slot.pointVotes == 0)  p.pointing = false;

    // A click only counts once the thumb has been seen open while pointing,
    // so a fist that starts out with the thumb tucked in doesn't fire.
    p.clickDistance = thumbToIndexSide (p);

    if (! p.pointing)
    {
        slot.clickArmed = false;
        p.clicking = false;
    }
    else
    {
        if (p.clickDistance > clickOffMetres)
            slot.clickArmed = true;

        p.clicking = p.clicking ? p.clickDistance < clickOffMetres
                                : slot.clickArmed && p.clickDistance < clickOnMetres;
    }

    // Smooth in frame-relative units so the filter doesn't depend on the camera
    // resolution. Still hands get heavy smoothing, fast ones almost none.
    const float scale = (float) std::max (width, height);
    const float rawX = (p.x[HandPose::thumbTip] + p.x[HandPose::indexTip]) * 0.5f / scale;
    const float rawY = (p.y[HandPose::thumbTip] + p.y[HandPose::indexTip]) * 0.5f / scale;
    const float dt = fresh ? 0.0f : (float) std::clamp ((nowMs - slot.lastMs) / 1000.0, 0.001, 0.25);

    p.cursorX = slot.fx.filter (rawX, dt, 1.7f, 6.0f) * scale;
    p.cursorY = slot.fy.filter (rawY, dt, 1.7f, 6.0f) * scale;
    p.tipX = slot.tx.filter (p.x[HandPose::indexTip] / scale, dt, 1.7f, 6.0f) * scale;
    p.tipY = slot.ty.filter (p.y[HandPose::indexTip] / scale, dt, 1.7f, 6.0f) * scale;
    slot.lastMs = nowMs;
}

int HandLandmarker::process (const uint8_t* rgb, int width, int height, int stride, double nowMs)
{
    found.clear();

    if (rgb == nullptr || width <= 0 || height <= 0)
    {
        reset();
        return 0;
    }

    // ---- follow the hands we already have ---------------------------------
    for (auto& slot : slots)
    {
        if (! slot.active)
            continue;

        const auto previous = slot.pose;
        HandPose pose;

        if (! runLandmarks (rgb, width, height, stride, slot.roi, pose) || pose.presence < keepHandPresence)
        {
            slot.active = false;
            continue;
        }

        pose.id = previous.id;
        pose.pinching = previous.pinching;
        pose.pointing = previous.pointing;
        pose.clicking = previous.clicking;
        slot.pose = pose;
        slot.roi = roiFromPose (pose);
        updateCursor (slot, width, height, nowMs, false);
    }

    // Two crops can slide onto the same hand: keep the surer one.
    if (slots[0].active && slots[1].active
        && iou (boundsOf (slots[0].pose), boundsOf (slots[1].pose)) > 0.35f)
    {
        auto& weaker = slots[0].pose.presence >= slots[1].pose.presence ? slots[1] : slots[0];
        weaker.active = false;
    }

    // ---- look for new hands while a slot is free ---------------------------
    if (! slots[0].active || ! slots[1].active)
    {
        for (const auto& p : detectPalms (rgb, width, height, stride))
        {
            Slot* free = ! slots[0].active ? &slots[0] : (! slots[1].active ? &slots[1] : nullptr);

            if (free == nullptr)
                break;

            // Already tracking this one?
            bool known = false;

            for (const auto& s : slots)
            {
                if (! s.active)
                    continue;

                auto b = boundsOf (s.pose);
                const float mx = (b.x1 - b.x0) * 0.1f, my = (b.y1 - b.y0) * 0.1f;

                if (p.cx > b.x0 - mx && p.cx < b.x1 + mx && p.cy > b.y0 - my && p.cy < b.y1 + my)
                    known = true;
            }

            if (known)
                continue;

            HandPose pose;

            if (! runLandmarks (rgb, width, height, stride, roiFromPalm (p), pose) || pose.presence < newHandPresence)
                continue;

            // A second pass from the landmark box settles the crop the way
            // tracking will see it next frame.
            HandPose refined;
            const auto roi = roiFromPose (pose);

            if (runLandmarks (rgb, width, height, stride, roi, refined) && refined.presence >= newHandPresence)
                pose = refined;

            free->active = true;
            free->pose = pose;
            free->pose.id = nextId++;
            free->roi = roiFromPose (pose);
            updateCursor (*free, width, height, nowMs, true);
        }
    }

    for (const auto& s : slots)
        if (s.active)
            found.push_back (s.pose);

    return (int) found.size();
}

} // namespace sl
