#include "HandTracker.h"

#include <algorithm>
#include <cmath>

namespace sl
{

namespace
{
    constexpr int targetGridWidth = 192;

    inline uint8_t lumaOf (int r, int g, int b) noexcept
    {
        return (uint8_t) std::clamp ((r * 299 + g * 587 + b * 114) / 1000, 0, 255);
    }

    inline bool isSkin (int r, int g, int b, float sensitivity) noexcept
    {
        const float y  = 0.299f * (float) r + 0.587f * (float) g + 0.114f * (float) b;
        const float cb = 128.0f - 0.168736f * (float) r - 0.331264f * (float) g + 0.5f * (float) b;
        const float cr = 128.0f + 0.5f * (float) r - 0.418688f * (float) g - 0.081312f * (float) b;

        const float cbLo = 76.0f  - 14.0f * sensitivity;
        const float cbHi = 128.0f + 14.0f * sensitivity;
        const float crLo = 132.0f - 16.0f * sensitivity;
        const float crHi = 176.0f + 16.0f * sensitivity;

        if (y < 35.0f || y > 250.0f)
            return false;

        const int maxc = std::max (r, std::max (g, b));
        const int minc = std::min (r, std::min (g, b));

        // Grey walls, white shirts, shadows and paperwork carry almost no
        // chroma: without this, the whole frame counts as skin.
        if (maxc - minc < 14)
            return false;

        if (cr - cb < 10.0f)
            return false;

        if (cb >= cbLo && cb <= cbHi && cr >= crLo && cr <= crHi)
            return true;

        // Second opinion for very warm or very dim light.
        return r > 70 && g > 30 && b > 15
            && std::abs (r - g) > 10 && r > g && r > b;
    }
}

void HandTracker::prepare (int frameWidth, int frameHeight)
{
    frameW = std::max (1, frameWidth);
    frameH = std::max (1, frameHeight);

    const int step = std::max (1, frameW / targetGridWidth);

    gridW = std::max (8, frameW / step);
    gridH = std::max (6, frameH / step);

    luma.assign ((size_t) gridW * (size_t) gridH, 0);
    prevLuma.assign ((size_t) gridW * (size_t) gridH, 0);
    mask.assign ((size_t) gridW * (size_t) gridH, 0);
    havePrev = false;
    reset();
}

void HandTracker::setLandmarker (std::unique_ptr<HandLandmarker> l)
{
    landmarker = std::move (l);
    reset();
}

void HandTracker::reset()
{
    if (landmarker != nullptr)
        landmarker->reset();

    for (auto& h : hands)
        h = {};

    for (auto& t : tracks)
        resetTrack (t);

    havePrev = false;
}

void HandTracker::resetTrack (Track& t)
{
    t = {};
}

void HandTracker::analyse (const uint8_t* rgb, int frameWidth, int frameHeight, int rowStrideBytes,
                           float sensitivity01, double nowMs)
{
    if (rgb == nullptr || frameWidth <= 0 || frameHeight <= 0)
    {
        reset();
        return;
    }

    clockMs = nowMs >= 0.0 ? nowMs : clockMs + 1000.0 / 30.0;

    if (landmarker != nullptr)
        analyseModel (rgb, frameWidth, frameHeight, rowStrideBytes, clockMs);
    else
        analyseHeuristic (rgb, frameWidth, frameHeight, rowStrideBytes, sensitivity01);
}

void HandTracker::analyseModel (const uint8_t* rgb, int frameWidth, int frameHeight, int rowStrideBytes,
                                double nowMs)
{
    // The debug mask belongs to the heuristic; the stage draws the skeleton.
    gridW = gridH = 0;
    mask.clear();

    landmarker->process (rgb, frameWidth, frameHeight, rowStrideBytes, nowMs);

    for (auto& h : hands)
        h = {};

    // The hands that can aim right now, with the point each one aims at.
    struct Aimed { const HandPose* pose; float x, y; bool trigger; };
    Aimed aimed[2];
    int count = 0;

    for (const auto& p : landmarker->hands())
    {
        if (count == 2)
            break;

        if (aim == AimMode::Fingertip)
        {
            if (p.pointing)
                aimed[count++] = { &p, p.tipX, p.tipY, p.clicking };
        }
        else
        {
            aimed[count++] = { &p, p.cursorX, p.cursorY, p.pinching };
        }
    }

    // Two hands: the one further left plays the left rail. One hand: whichever
    // half of the picture it is in.
    int order[2] { -1, -1 };

    if (count == 2)
    {
        const bool firstIsLeft = aimed[0].x <= aimed[1].x;
        order[0] = firstIsLeft ? 0 : 1;
        order[1] = firstIsLeft ? 1 : 0;
    }
    else if (count == 1)
    {
        order[aimed[0].x < (float) frameWidth * 0.5f ? 0 : 1] = 0;
    }

    const float sx = 1.0f / (float) frameWidth, sy = 1.0f / (float) frameHeight;

    for (int s = 0; s < 2; ++s)
    {
        if (order[s] < 0)
            continue;

        const auto& a = aimed[order[s]];
        HandPose p = *a.pose;

        for (int i = 0; i < HandPose::numPoints; ++i)
        {
            p.x[i] *= sx;
            p.y[i] *= sy;
        }

        p.cursorX *= sx;
        p.cursorY *= sy;
        p.tipX *= sx;
        p.tipY *= sy;
        poses[s] = p;

        float x0 = 1.0f, x1 = 0.0f, y0 = 1.0f, y1 = 0.0f;

        for (int i = 0; i < HandPose::numPoints; ++i)
        {
            x0 = std::min (x0, p.x[i]);  x1 = std::max (x1, p.x[i]);
            y0 = std::min (y0, p.y[i]);  y1 = std::max (y1, p.y[i]);
        }

        auto& out = hands[s];
        out.detected = true;
        out.x = std::clamp (a.x * sx, 0.0f, 1.0f);
        out.y = std::clamp (a.y * sy, 0.0f, 1.0f);
        out.size = std::max (0.0f, (x1 - x0) * (y1 - y0));
        out.confidence = p.presence;
        out.hasPinch = true;
        out.pinching = a.trigger;
        out.pointing = p.pointing;
    }
}

void HandTracker::analyseHeuristic (const uint8_t* rgb, int frameWidth, int frameHeight, int rowStrideBytes,
                                    float sensitivity01)
{
    if (frameWidth != frameW || frameHeight != frameH || gridW <= 0)
        prepare (frameWidth, frameHeight);

    const float sensitivity = std::clamp (sensitivity01, 0.0f, 1.0f);
    const int step = std::max (1, frameWidth / gridW);
    const int motionThreshold = (int) (26.0f - 16.0f * sensitivity);

    // --- sample the frame into a small grid ---------------------------------
    for (int gy = 0; gy < gridH; ++gy)
    {
        const int sy = std::min (frameHeight - 1, gy * step);
        const uint8_t* row = rgb + (size_t) sy * (size_t) rowStrideBytes;

        for (int gx = 0; gx < gridW; ++gx)
        {
            const int sx = std::min (frameWidth - 1, gx * step);
            const uint8_t* p = row + (size_t) sx * 3u;

            luma[(size_t) gy * (size_t) gridW + (size_t) gx] = lumaOf (p[0], p[1], p[2]);
        }
    }

    // --- per side: skin and motion evidence ---------------------------------
    for (int s = 0; s < 2; ++s)
    {
        const float regionLo = s == 0 ? 0.0f : 0.63f;
        const float regionHi = s == 0 ? 0.37f : 1.0f;

        const int gx0 = std::clamp ((int) (regionLo * (float) gridW), 0, gridW - 1);
        const int gx1 = std::clamp ((int) (regionHi * (float) gridW), gx0 + 1, gridW);

        // Score coarse cells, then refine around the strongest one. A single
        // weighted centroid would drift towards the face or a brightly lit wall.
        constexpr int cellsX = 5, cellsY = 10;
        float bestScore = 0.0f;
        int bestCellX = -1, bestCellY = -1;
        float totalSkin = 0.0f, totalMotion = 0.0f;

        const float cellW = (float) (gx1 - gx0) / (float) cellsX;
        const float cellH = (float) gridH / (float) cellsY;

        for (int cy = 0; cy < cellsY; ++cy)
        {
            for (int cx = 0; cx < cellsX; ++cx)
            {
                float score = 0.0f;

                const int gyLo = (int) (cy * cellH);
                const int gyHi = std::min (gridH, (int) ((cy + 1) * cellH));

                for (int gy = gyLo; gy < gyHi; ++gy)
                {
                    for (int gx = gx0 + (int) (cx * cellW); gx < gx0 + (int) ((cx + 1) * cellW); ++gx)
                    {
                        const size_t idx = (size_t) gy * (size_t) gridW + (size_t) gx;
                        const int diff = havePrev ? std::abs ((int) luma[idx] - (int) prevLuma[idx]) : 0;
                        const bool moving = diff > motionThreshold;

                        const int sy = std::min (frameHeight - 1, gy * step);
                        const int sx = std::min (frameWidth - 1, gx * step);
                        const uint8_t* p = rgb + (size_t) sy * (size_t) rowStrideBytes + (size_t) sx * 3u;

                        const bool skin = isSkin (p[0], p[1], p[2], sensitivity);

                        if (skin)
                        {
                            score += 2.0f;
                            totalSkin += 1.0f;
                        }

                        if (moving)
                        {
                            score += 1.0f;
                            totalMotion += 1.0f;
                        }
                    }
                }

                if (score > bestScore)
                {
                    bestScore = score;
                    bestCellX = cx;
                    bestCellY = cy;
                }
            }
        }

        const float regionPixels = (float) ((gx1 - gx0) * gridH);
        const float minPixels = std::max (12.0f, regionPixels * 0.004f);

        HandSample& out = hands[s];

        if (bestCellX < 0 || (totalSkin < minPixels && totalMotion < minPixels))
        {
            out = {};
            if (auto& t = tracks[s]; t.valid)
            {
                if (++t.missed > 8)
                    resetTrack (t);
            }

            continue;
        }

        // Weighted centroid of the pixels around the winning cell.
        const float centreGX = (float) (gx0 + (int) ((bestCellX + 0.5f) * cellW));
        const float centreGY = (float) ((bestCellY + 0.5f) * cellH);
        const float radiusX = std::max (4.0f, cellW * 1.6f);
        const float radiusY = std::max (4.0f, cellH * 1.6f);

        const int gyLo = std::max (0, (int) (centreGY - radiusY));
        const int gyHi = std::min (gridH, (int) (centreGY + radiusY) + 1);
        const int gxLo = std::max (gx0, (int) (centreGX - radiusX));
        const int gxHi = std::min (gx1, (int) (centreGX + radiusX) + 1);

        double sumW = 0.0, sumX = 0.0, sumY = 0.0;
        float blobPixels = 0.0f;

        for (int gy = gyLo; gy < gyHi; ++gy)
        {
            for (int gx = gxLo; gx < gxHi; ++gx)
            {
                const size_t idx = (size_t) gy * (size_t) gridW + (size_t) gx;
                const int diff = havePrev ? std::abs ((int) luma[idx] - (int) prevLuma[idx]) : 0;
                const bool moving = diff > motionThreshold;

                const int sy = std::min (frameHeight - 1, gy * step);
                const int sx = std::min (frameWidth - 1, gx * step);
                const uint8_t* p = rgb + (size_t) sy * (size_t) rowStrideBytes + (size_t) sx * 3u;

                const bool skin = isSkin (p[0], p[1], p[2], sensitivity);
                const float w = (skin ? 2.0f : 0.0f) + (moving ? 1.0f : 0.0f);

                if (w > 0.0f)
                {
                    sumW += (double) w;
                    sumX += (double) w * (double) gx;
                    sumY += (double) w * (double) gy;
                    blobPixels += 1.0f;
                }
            }
        }

        if (sumW <= 0.0)
        {
            out = {};
            continue;
        }

        Track& t = tracks[s];
        float nx = (float) (sumX / sumW) / (float) (gridW - 1);
        float ny = (float) (sumY / sumW) / (float) (gridH - 1);

        if (! t.valid)
        {
            t.x = nx;
            t.y = ny;
            t.valid = true;
        }
        else
        {
            // Light smoothing: enough to stop the highlight flickering between
            // two neighbouring keys, little enough to keep the response instant.
            t.x += (nx - t.x) * 0.5f;
            t.y += (ny - t.y) * 0.5f;
        }

        t.missed = 0;
        t.size = blobPixels / std::max (1.0f, regionPixels);

        out.detected = true;
        out.x = t.x;
        out.y = t.y;
        out.size = t.size;
        out.confidence = std::clamp ((totalSkin * 2.0f + totalMotion) / (regionPixels * 0.02f), 0.0f, 1.0f);
    }

    // --- debug mask ---------------------------------------------------------
    for (int gy = 0; gy < gridH; ++gy)
    {
        for (int gx = 0; gx < gridW; ++gx)
        {
            const size_t idx = (size_t) gy * (size_t) gridW + (size_t) gx;
            const int diff = havePrev ? std::abs ((int) luma[idx] - (int) prevLuma[idx]) : 0;
            const bool moving = diff > motionThreshold;

            const int sy = std::min (frameHeight - 1, gy * step);
            const int sx = std::min (frameWidth - 1, gx * step);
            const uint8_t* p = rgb + (size_t) sy * (size_t) rowStrideBytes + (size_t) sx * 3u;
            const bool skin = isSkin (p[0], p[1], p[2], sensitivity);

            mask[idx] = skin ? (moving ? 255 : 170) : (moving ? 90 : 0);
        }
    }

    std::swap (luma, prevLuma);
    havePrev = true;
}

} // namespace sl
