#include "StagePanel.h"

namespace sl::ui
{

StagePanel::StagePanel (SightLineProcessor& p)
    : overlaySwitch ({ "Keys", "Scale", "Drums" }, pal::teal),
      proc (p)
{
    addAndMakeVisible (overlaySwitch);
    addAndMakeVisible (trackingPill);
    trackingPill.button.setTooltip ("Show what the hand tracker sees: the hand skeleton (or skin and motion for the basic tracker), hand positions and fps");
    trackingPill.button.onClick = [this] { setTrackingDebug (trackingPill.button.getToggleState()); };
    syncOverlay();

    overlaySwitch.onChange = [this] (int index)
    {
        if (auto* param = proc.apvts.getParameter ("overlay"))
            param->setValueNotifyingHost (param->convertTo0to1 ((float) index));

        syncOverlay();
    };

    setMouseCursor (juce::MouseCursor::NormalCursor);
}

void StagePanel::syncOverlay()
{
    const int current = (int) proc.currentOverlay();

    if (current == shownOverlay)
        return;

    shownOverlay = current;
    overlaySwitch.setSelected (current, false);
    overlaySwitch.setAccent (accentFor ((Overlay) current));
    repaint();
}

Rect StagePanel::visibleFrameArea (int frameWidth, int frameHeight) const
{
    if (frameWidth <= 0 || frameHeight <= 0 || getWidth() <= 0 || getHeight() <= 0)
        return { 0.0f, 0.0f, 1.0f, 1.0f };

    const float stageAspect = (float) getWidth() / (float) getHeight();
    const float frameAspect = (float) frameWidth / (float) frameHeight;

    // Matches RectanglePlacement::fillDestination | centred in paint().
    if (stageAspect >= frameAspect)
    {
        const float h = frameAspect / stageAspect;
        return { 0.0f, (1.0f - h) * 0.5f, 1.0f, h };
    }

    const float w = stageAspect / frameAspect;
    return { (1.0f - w) * 0.5f, 0.0f, w, 1.0f };
}

HandSample StagePanel::frameToStage (HandSample hand, int frameWidth, int frameHeight) const
{
    // The crop is centred, so this commutes with mirroring.
    const auto v = visibleFrameArea (frameWidth, frameHeight);
    hand.x = (hand.x - v.x) / v.w;
    hand.y = (hand.y - v.y) / v.h;
    return hand;
}

void StagePanel::setFrame (const juce::Image& image)
{
    frame = image;
    repaint();
}

void StagePanel::setCameraStatus (const juce::String& name, bool live)
{
    if (cameraName == name && cameraLive == live)
        return;

    cameraName = name.isNotEmpty() ? name : juce::String ("NO CAMERA");
    cameraLive = live;

    // Don't leave the last picture frozen on screen once the camera is gone.
    if (! live)
    {
        frame = {};
        mask.clear();
        maskW = maskH = 0;
        maskImage = {};
    }

    repaint();
}

void StagePanel::setOverlayText (const juce::String& text)
{
    if (overlayText == text)
        return;

    overlayText = text;
    repaint();
}

void StagePanel::resized()
{
    // Top centre: the rails run nearly the full height down both sides, and the
    // middle of the picture (the player's face) has no targets.
    overlaySwitch.setBounds (getLocalBounds().withSizeKeepingCentre (236, 30).withY (12));
    trackingPill.setBounds (getLocalBounds().withSizeKeepingCentre (132, 28).withY (50));
}

void StagePanel::paint (juce::Graphics& g)
{
    // TEMP DIAG
    const auto diagPaintStart = juce::Time::getMillisecondCounterHiRes();

    auto area = getLocalBounds().toFloat();
    const auto accent = accentFor (proc.currentOverlay());

    drawPanel (g, area, 14.0f, pal::panel);

    {
        juce::Graphics::ScopedSaveState save (g);
        juce::Path clip;
        clip.addRoundedRectangle (area, 14.0f);
        g.reduceClipRegion (clip);

        if (frame.isValid())
        {
            {
                // The editor hands over the frame already mirrored, so it is
                // drawn straight; the overlay is in the same display space.
                g.drawImage (frame, area, juce::RectanglePlacement::centred
                                            | juce::RectanglePlacement::fillDestination);
            }

            // A little shade so the translucent keys stay readable on a bright room.
            g.setColour (juce::Colours::black.withAlpha (0.16f));
            g.fillRect (area);

            if (trackingDebug)
            {
                drawTrackingOverlay (g, area);
                drawSkeletons (g, area);
            }
        }
        else
        {
            drawPlaceholder (g, area);
        }

        drawRails (g, area);
        drawHands (g, area);

        if (trackingDebug)
            drawTrackingReadout (g, area);

        if (const int pad = proc.learningPad(); pad >= 0)
            drawLearnBanner (g, area, pad);
    }

    // ---- floating status -------------------------------------------------
    auto live = proc.getLiveState();

    const juce::String status = cameraLive
        ? cameraName.toUpperCase() + "   " + juce::String (juce::roundToInt (live.fps)) + " FPS"
        : juce::String ("NO CAMERA");

    g.setColour (cameraLive ? pal::dim : pal::pink);
    g.setFont (uiFont (10.0f, true));
    g.drawText (status, area.withTrimmedRight (16.0f).removeFromTop (34.0f),
                juce::Justification::centredRight, false);

    // ---- what is sounding ------------------------------------------------
    juce::String notes;

    for (int s = 0; s < 2; ++s)
    {
        if (live.activeNote[s] >= 0)
        {
            if (notes.isNotEmpty())
                notes << "  ·  ";

            // The target's own label, so a drum pad reads "Snare", not "D1".
            juce::String label (midiNoteName (live.activeNote[s]).c_str());

            for (const auto& z : proc.getZones())
                if (z.side == s && z.note == live.activeNote[s])
                    label = juce::String (z.label.c_str());

            notes << (s == 0 ? "L  " : "R  ") << label;
        }
    }

    if (notes.isNotEmpty())
    {
        const auto font = uiFont (12.0f, true);
        auto textArea = area.removeFromBottom (54.0f).withTrimmedBottom (16.0f);
        const float width = textWidth (font, notes) + 32.0f;
        auto pill = juce::Rectangle<float> (textArea.getCentreX() - width * 0.5f, textArea.getY(),
                                            width, 26.0f);

        g.setColour (pal::bg.withAlpha (0.8f));
        g.fillRoundedRectangle (pill, 13.0f);
        g.setColour (accent.withAlpha (0.8f));
        g.drawRoundedRectangle (pill.reduced (0.5f), 13.0f, 1.0f);
        g.setColour (pal::text);
        g.setFont (font);
        g.drawText (notes, pill, juce::Justification::centred, false);
    }
    else if (overlayText.isNotEmpty())
    {
        g.setColour (pal::text.withAlpha (0.55f));
        g.setFont (uiFont (11.0f));
        g.drawText (overlayText, area.removeFromBottom (46.0f).reduced (24.0f, 16.0f),
                    juce::Justification::centred, false);
    }

    // TEMP DIAG
    {
        const auto diagPaintEnd = juce::Time::getMillisecondCounterHiRes();
        static int diagPaints = 0;

        if (++diagPaints % 30 == 0)
            std::fprintf (stderr, "[diag] paint %.2f ms\n", diagPaintEnd - diagPaintStart);
    }
}

void StagePanel::setTrackingMask (const std::vector<uint8_t>& values, int width, int height)
{
    if (width <= 0 || height <= 0 || values.size() < (size_t) width * (size_t) height)
    {
        mask.clear();
        maskW = maskH = 0;
        maskImage = {};
        return;
    }

    mask = values;
    maskW = width;
    maskH = height;

    if (trackingDebug)
        rebuildMaskImage();
}

void StagePanel::setSkeletons (const Skeleton (&hands)[2])
{
    skeletons[0] = hands[0];
    skeletons[1] = hands[1];
}

void StagePanel::setTrackerInfo (const juce::String& text, bool usingModel)
{
    trackerInfo = text;
    trackerIsModel = usingModel;
    repaint();
}

void StagePanel::drawSkeletons (juce::Graphics& g, juce::Rectangle<float> area)
{
    static constexpr int bones[][2] {
        { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 4 },
        { 0, 5 }, { 5, 6 }, { 6, 7 }, { 7, 8 },
        { 5, 9 }, { 9, 10 }, { 10, 11 }, { 11, 12 },
        { 9, 13 }, { 13, 14 }, { 14, 15 }, { 15, 16 },
        { 13, 17 }, { 0, 17 }, { 17, 18 }, { 18, 19 }, { 19, 20 }
    };

    for (int s = 0; s < 2; ++s)
    {
        const auto& sk = skeletons[s];

        if (! sk.valid)
            continue;

        const auto at = [&] (int i)
        {
            return juce::Point<float> { area.getX() + sk.points[i].x * area.getWidth(),
                                        area.getY() + sk.points[i].y * area.getHeight() };
        };

        const auto colour = s == 0 ? pal::teal : pal::pink;
        g.setColour (colour.withAlpha (0.8f));

        for (const auto& b : bones)
            g.drawLine ({ at (b[0]), at (b[1]) }, 1.6f);

        for (int i = 0; i < HandPose::numPoints; ++i)
        {
            const bool tip = i == HandPose::thumbTip || i == HandPose::indexTip;
            const float d = tip || (sk.pointing && i == HandPose::indexTip) ? 7.0f : 4.0f;
            g.setColour (tip && sk.pinching ? pal::yellow
                         : (sk.pointing && i == HandPose::indexTip ? juce::Colours::white : colour));
            g.fillEllipse (juce::Rectangle<float> (d, d).withCentre (at (i)));
        }
    }
}

void StagePanel::setTrackingDebug (bool shouldShow)
{
    if (trackingDebug == shouldShow)
        return;

    trackingDebug = shouldShow;
    trackingPill.button.setToggleState (shouldShow, juce::dontSendNotification);
    rebuildMaskImage();
    repaint();
}

void StagePanel::rebuildMaskImage()
{
    if (! trackingDebug || maskW <= 0 || maskH <= 0 || mask.empty())
    {
        maskImage = {};
        return;
    }

    if (! maskImage.isValid() || maskImage.getWidth() != maskW || maskImage.getHeight() != maskH)
        maskImage = juce::Image (juce::Image::ARGB, maskW, maskH, true, juce::SoftwareImageType());

    const auto skin = pal::teal;
    const auto motion = pal::amber;
    juce::Image::BitmapData pixels (maskImage, juce::Image::BitmapData::writeOnly);

    for (int y = 0; y < maskH; ++y)
    {
        for (int x = 0; x < maskW; ++x)
        {
            const int sourceX = proc.isMirrored() ? maskW - 1 - x : x;
            const auto v = mask[(size_t) y * (size_t) maskW + (size_t) sourceX];
            juce::Colour c = juce::Colours::transparentBlack;

            if (v == 255)       c = skin.brighter (0.55f);
            else if (v == 170)  c = skin;
            else if (v == 90)   c = motion;

            pixels.setPixelColour (x, y, c);
        }
    }
}

void StagePanel::drawTrackingOverlay (juce::Graphics& g, juce::Rectangle<float> area)
{
    if (! frame.isValid())
        return;

    const auto visible = visibleFrameArea (frame.getWidth(), frame.getHeight());
    const auto edgeAt = [&] (float nx)
    {
        return area.getX() + (nx - visible.x) / visible.w * area.getWidth();
    };

    // The landmark model searches the whole picture; only the basic tracker
    // has search regions and a mask to show.
    if (trackerIsModel)
        return;

    const float leftEdge = juce::jlimit (area.getX(), area.getRight(), edgeAt (0.37f));
    const float rightEdge = juce::jlimit (area.getX(), area.getRight(), edgeAt (0.63f));

    // The middle band is never searched: shade it so it is obvious why a hand
    // resting there does nothing.
    g.setColour (juce::Colours::black.withAlpha (0.22f));
    g.fillRect (juce::Rectangle<float> (leftEdge, area.getY(), rightEdge - leftEdge, area.getHeight()));

    if (maskImage.isValid())
    {
        juce::Graphics::ScopedSaveState state (g);
        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);

        // The mask grid is flipped with the picture when it is built, so the
        // cells land on the pixels they were sampled from. It is opaque except
        // where the tracker saw nothing, so the
        // translucency comes from the graphics opacity rather than per-pixel
        // alpha, which the scaled software-image path blends unreliably.
        g.setOpacity (0.45f);
        g.drawImage (maskImage, area, juce::RectanglePlacement::centred
                                    | juce::RectanglePlacement::fillDestination);
    }

    g.setColour (pal::yellow.withAlpha (0.55f));
    g.drawVerticalLine (juce::roundToInt (leftEdge), area.getY(), area.getBottom());
    g.drawVerticalLine (juce::roundToInt (rightEdge), area.getY(), area.getBottom());
}

void StagePanel::drawTrackingReadout (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto live = proc.getLiveState();
    auto card = juce::Rectangle<float> (area.getCentreX() - 127.0f, area.getY() + 86.0f, 254.0f,
                                        trackerIsModel ? 88.0f : 112.0f);

    g.setColour (pal::bg.withAlpha (0.82f));
    g.fillRoundedRectangle (card, 10.0f);
    g.setColour (pal::yellow.withAlpha (0.4f));
    g.drawRoundedRectangle (card.reduced (0.5f), 10.0f, 1.0f);

    auto r = card.reduced (12.0f, 9.0f);
    auto header = r.removeFromTop (15.0f);

    g.setColour (pal::yellow);
    g.setFont (uiFont (9.5f, true));
    g.drawText ("TRACKING TEST", header, juce::Justification::centredLeft, false);

    g.setColour (pal::text.withAlpha (0.8f));
    g.drawText (juce::String (juce::roundToInt (live.fps)) + " FPS", header,
                juce::Justification::centredRight, false);

    for (int s = 0; s < 2; ++s)
    {
        auto line = r.removeFromTop (17.0f);
        auto tag = line.removeFromLeft (16.0f);

        g.setColour (s == 0 ? pal::teal : pal::pink);
        g.setFont (uiFont (10.5f, true));
        g.drawText (s == 0 ? "L" : "R", tag, juce::Justification::centredLeft, false);

        juce::String text;

        if (live.handSeen[s])
        {
            text << "x " << juce::String (live.handX[s], 2)
                 << "  y " << juce::String (live.handY[s], 2);

            if (live.hasPinch[s])
                text << (live.pointing[s] ? (live.pinching[s] ? "  point+click" : "  point")
                                          : (live.pinching[s] ? "  pinch" : "  open"));

            const auto& zones = proc.getZones();
            const int hovered = live.hovered[s];

            if (hovered >= 0 && hovered < (int) zones.size())
                text << "   " << juce::String (zones[(size_t) hovered].label.c_str());
            else if (live.activeNote[s] >= 0)
                text << "   " << juce::String (midiNoteName (live.activeNote[s]).c_str());
            else
                text << "   no target";
        }
        else
        {
            text = "no hand";
        }

        g.setColour (live.handSeen[s] ? pal::text : pal::dim);
        g.setFont (uiFont (10.5f));
        g.drawText (text, line, juce::Justification::centredLeft, false);
    }

    if (trackerIsModel || trackerInfo.isNotEmpty())
    {
        // Which tracker is running (and, for the basic one, why).
        g.setColour (trackerIsModel ? pal::teal : pal::amber);
        g.setFont (uiFont (9.0f));
        g.drawFittedText (trackerInfo, r.removeFromBottom (13.0f).toNearestInt(),
                          juce::Justification::centredLeft, 1, 0.8f);

        if (trackerIsModel)
            return;
    }

    // What the mask colours mean.
    auto legend = r.removeFromBottom (13.0f);
    const auto swatch = [&] (juce::Colour colour, const juce::String& name)
    {
        g.setColour (colour);
        g.fillEllipse (legend.removeFromLeft (7.0f).withSizeKeepingCentre (7.0f, 7.0f));
        legend.removeFromLeft (4.0f);

        g.setColour (pal::dim);
        g.setFont (uiFont (9.0f));
        g.drawText (name, legend.removeFromLeft (textWidth (uiFont (9.0f), name) + 4.0f),
                    juce::Justification::centredLeft, false);
        legend.removeFromLeft (10.0f);
    };

    swatch (pal::teal.withAlpha (0.55f), "skin");
    swatch (pal::amber.withAlpha (0.55f), "motion");
    swatch (pal::teal.brighter (0.5f), "both");
}

void StagePanel::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu() || proc.currentOverlay() != Overlay::Drums)
        return;

    const auto area = getLocalBounds().toFloat();
    const auto& zones = proc.getZones();

    for (int i = 0; i < (int) zones.size(); ++i)
    {
        const auto& z = zones[(size_t) i];

        if (toStage (area, z.bounds).contains (e.position))
        {
            showPadMenu (i - z.side * numDrumPads);
            return;
        }
    }
}

void StagePanel::showPadMenu (int pad)
{
    const int current = proc.drumNote (pad);
    const int gm = defaultDrumNotes()[(size_t) pad];

    juce::PopupMenu menu;
    menu.addSectionHeader (juce::String (drumPadName (pad)) + "   "
                           + juce::String::charToString ((juce::juce_wchar) 0x00b7)
                           + "   note " + juce::String (current));
    menu.addItem (1, "MIDI learn");
    menu.addItem (2, "Reset to GM note (" + juce::String (gm) + ")", current != gm);

    if (proc.learningPad() >= 0)
        menu.addItem (3, "Cancel learn");

    juce::Component::SafePointer<StagePanel> safe (this);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                        [safe, pad] (int result)
                        {
                            if (safe == nullptr)
                                return;

                            if (result == 1)      safe->proc.startDrumLearn (pad);
                            else if (result == 2) safe->proc.resetDrumNote (pad);
                            else if (result == 3) safe->proc.cancelDrumLearn();

                            safe->repaint();
                        });
}

void StagePanel::drawLearnBanner (juce::Graphics& g, juce::Rectangle<float> area, int pad)
{
    const juce::String text = "MIDI LEARN  " + juce::String::charToString ((juce::juce_wchar) 0x00b7)
                            + "  " + juce::String (drumPadName (pad))
                            + "  " + juce::String::charToString ((juce::juce_wchar) 0x00b7)
                            + "  play a note on your controller";
    const auto font = uiFont (11.5f, true);
    const float width = textWidth (font, text) + 36.0f;

    auto chip = juce::Rectangle<float> (width, 28.0f)
                    .withCentre ({ area.getCentreX(), area.getBottom() - 104.0f });

    g.setColour (pal::bg.withAlpha (0.86f));
    g.fillRoundedRectangle (chip, 14.0f);
    g.setColour (pal::yellow.withAlpha (0.9f));
    g.drawRoundedRectangle (chip.reduced (0.5f), 14.0f, 1.2f);
    g.setColour (pal::yellow);
    g.setFont (font);
    g.drawText (text, chip, juce::Justification::centred, false);
}

void StagePanel::drawPlaceholder (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (pal::panelSoft);
    g.fillRect (area);

    g.setColour (pal::stroke.withAlpha (0.5f));

    for (float x = area.getX(); x < area.getRight(); x += 28.0f)
        g.drawLine (x, area.getY(), x, area.getBottom(), 0.5f);

    for (float y = area.getY(); y < area.getBottom(); y += 28.0f)
        g.drawLine (area.getX(), y, area.getRight(), y, 0.5f);

    auto centre = area.withSizeKeepingCentre (420.0f, 120.0f);

    g.setColour (pal::dim);
    g.setFont (uiFont (14.0f, true));
    g.drawText ("No camera feed", centre.removeFromTop (26.0f), juce::Justification::centred, false);

    g.setColour (pal::dim.withAlpha (0.8f));
    g.setFont (uiFont (11.0f));
    g.drawFittedText ("Choose a camera on the Setup page. If this is the first time, "
                      "Windows may ask whether sightline can use it.",
                      centre.toNearestInt(), juce::Justification::top, 3);
}

void StagePanel::drawRails (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto overlay = proc.currentOverlay();
    const auto accent = accentFor (overlay);
    const int learnPad = overlay == Overlay::Drums ? proc.learningPad() : -1;
    const auto live = proc.getLiveState();
    const float opacity = proc.overlayOpacity();

    const Rect rails[2] { leftRailBounds(), rightRailBounds() };

    for (const auto& rn : rails)
    {
        auto rail = toStage (area, rn);

        g.setColour (pal::bg.withAlpha (0.28f + 0.10f * opacity));
        g.fillRoundedRectangle (rail, 10.0f);
        g.setColour (accent.withAlpha (0.16f));
        g.drawRoundedRectangle (rail.reduced (0.5f), 10.0f, 1.0f);
    }

    for (int i = 0; i < (int) proc.getZones().size(); ++i)
    {
        const auto& z = proc.getZones()[(size_t) i];
        auto r = toStage (area, z.bounds).reduced (2.0f, 1.0f);

        const bool active = live.activeNote[z.side] == z.note && live.activeNote[z.side] >= 0;
        const bool hovered = live.hovered[z.side] == i;
        const float baseAlpha = z.blackKey ? 0.05f + 0.12f * opacity : 0.10f + 0.18f * opacity;

        if (active)
        {
            g.setColour (accent.withAlpha (0.62f));
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (accent.brighter (0.25f));
            g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.4f);
        }
        else if (hovered)
        {
            g.setColour (accent.withAlpha (0.26f + 0.10f * live.dwell[z.side]));
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (accent.withAlpha (0.55f));
            g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.2f);
        }
        else
        {
            g.setColour (juce::Colours::white.withAlpha (baseAlpha));
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (pal::stroke.withAlpha (0.45f));
            g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 0.8f);
        }

        // Dwell: a thin line fills across the target as the note arms.
        if (hovered && ! active && live.dwell[z.side] > 0.01f)
        {
            auto bar = r.removeFromBottom (2.5f).reduced (5.0f, 0.0f);
            g.setColour (accent.withAlpha (0.85f));
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * live.dwell[z.side]), 1.2f);
        }

        if (learnPad >= 0 && i - z.side * numDrumPads == learnPad)
        {
            g.setColour (pal::yellow.withAlpha (0.16f));
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (pal::yellow.withAlpha (0.9f));
            g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 2.0f);
        }

        if (r.getHeight() >= 15.0f)
        {
            const float fontSize = juce::jlimit (8.0f, 13.0f, r.getHeight() * 0.46f);

            g.setColour (active ? juce::Colours::black.withAlpha (0.85f)
                                : pal::text.withAlpha (z.blackKey ? 0.55f : 0.78f));
            g.setFont (uiFont (fontSize, active));
            g.drawText (juce::String (z.label.c_str()), r.reduced (4.0f, 0.0f),
                        juce::Justification::centred, false);

            // Scale degree / GM note number, tucked into the outer corner.
            if (! z.subLabel.empty() && r.getWidth() >= 90.0f)
            {
                g.setColour (active ? juce::Colours::black.withAlpha (0.6f) : pal::dim);
                g.setFont (uiFont (juce::jmax (8.0f, fontSize - 3.0f)));
                g.drawText (juce::String (z.subLabel.c_str()), r.reduced (8.0f, 0.0f),
                            z.side == 0 ? juce::Justification::centredLeft
                                        : juce::Justification::centredRight, false);
            }
        }
    }
}

void StagePanel::drawHands (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto live = proc.getLiveState();
    const auto accent = accentFor (proc.currentOverlay());

    for (int s = 0; s < 2; ++s)
    {
        if (! live.handSeen[s])
            continue;

        const juce::Point<float> p { area.getX() + live.handX[s] * area.getWidth(),
                                     area.getY() + live.handY[s] * area.getHeight() };
        // With the landmark model the cursor sits between thumb and index tip:
        // it closes into a solid dot while they pinch, and stays an open ring
        // while the hand is only pointing.
        const bool pinched = live.hasPinch[s] && live.pinching[s];
        const float radius = pinched ? 13.0f : 22.0f;

        g.setColour (accent.withAlpha (pinched ? 0.95f : 0.75f));
        g.drawEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (p),
                       pinched ? 2.6f : 1.6f);

        if (pinched)
        {
            g.setColour (accent.withAlpha (0.35f));
            g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (p));
            g.setColour (accent);
        }

        g.fillEllipse (juce::Rectangle<float> (pinched ? 8.0f : 5.0f, pinched ? 8.0f : 5.0f).withCentre (p));

        g.setColour (pal::text.withAlpha (0.55f));
        g.drawLine (p.x - radius - 7.0f, p.y, p.x - radius - 2.0f, p.y, 1.2f);
        g.drawLine (p.x + radius + 2.0f, p.y, p.x + radius + 7.0f, p.y, 1.2f);
    }
}

} // namespace sl::ui
