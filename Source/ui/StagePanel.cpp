#include "StagePanel.h"

namespace sl::ui
{

namespace
{
    juce::Colour accentFor (Overlay o)
    {
        switch (o)
        {
            case Overlay::Keys:  return pal::teal;
            case Overlay::Scale: return pal::purple;
            case Overlay::Drums: return pal::pink;
        }

        return pal::teal;
    }
}

StagePanel::StagePanel (SightLineProcessor& p)
    : proc (p),
      overlaySwitch ({ "Keys", "Scale", "Drums" }, pal::teal)
{
    addAndMakeVisible (overlaySwitch);
    overlaySwitch.setSelected ((int) proc.currentOverlay(), false);
    overlaySwitch.setAccent (accentFor (proc.currentOverlay()));

    overlaySwitch.onChange = [this] (int index)
    {
        if (auto* param = proc.apvts.getParameter ("overlay"))
            param->setValueNotifyingHost (param->convertTo0to1 ((float) index));

        overlaySwitch.setAccent (accentFor ((Overlay) index));
        overlayText.clear();
        repaint();
    };

    setMouseCursor (juce::MouseCursor::NormalCursor);
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
    auto area = getLocalBounds();
    overlaySwitch.setBounds (area.getX() + 16, area.getY() + 16, 236, 30);
}

void StagePanel::paint (juce::Graphics& g)
{
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
                // Mirror the picture only; the overlay is drawn afterwards,
                // unmirrored, so a key always sits where it looks like it does.
                juce::Graphics::ScopedSaveState flip (g);

                if (proc.isMirrored())
                    g.addTransform (juce::AffineTransform::translation (area.getCentreX(), 0.0f)
                                        .scaled (-1.0f, 1.0f)
                                        .translated (-area.getCentreX(), 0.0f));

                g.drawImage (frame, area);
            }

            // A little shade so the translucent keys stay readable on a bright room.
            g.setColour (juce::Colours::black.withAlpha (0.16f));
            g.fillRect (area);
        }
        else
        {
            drawPlaceholder (g, area);
        }

        drawRails (g, area);
        drawHands (g, area);
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

            notes << (s == 0 ? "L " : "R ") << juce::String (midiNoteName (live.activeNote[s]).c_str());
        }
    }

    if (notes.isNotEmpty())
    {
        const auto label = notes;
        auto textArea = area.removeFromBottom (54.0f).withTrimmedBottom (16.0f);
        const float width = juce::jmin (260.0f, 26.0f + (float) label.length() * 8.0f);
        auto pill = juce::Rectangle<float> (textArea.getCentreX() - width * 0.5f, textArea.getY(),
                                            width, 26.0f);

        g.setColour (pal::bg.withAlpha (0.8f));
        g.fillRoundedRectangle (pill, 13.0f);
        g.setColour (accent.withAlpha (0.8f));
        g.drawRoundedRectangle (pill.reduced (0.5f), 13.0f, 1.0f);
        g.setFont (uiFont (12.0f, true));
        g.drawText (label, pill, juce::Justification::centred, false);
    }
    else if (overlayText.isNotEmpty())
    {
        g.setColour (pal::text.withAlpha (0.55f));
        g.setFont (uiFont (11.0f));
        g.drawText (overlayText, area.removeFromBottom (46.0f).reduced (24.0f, 16.0f),
                    juce::Justification::centred, false);
    }
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
                      "Windows may ask whether SightLine can use it.",
                      centre.toNearestInt(), juce::Justification::top, 3);
}

void StagePanel::drawRails (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto overlay = proc.currentOverlay();
    const auto accent = accentFor (overlay);
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

        if (r.getHeight() >= 15.0f)
        {
            const float fontSize = juce::jlimit (8.0f, 13.0f, r.getHeight() * 0.46f);

            g.setColour (active ? juce::Colours::black.withAlpha (0.85f)
                                : pal::text.withAlpha (z.blackKey ? 0.55f : 0.78f));
            g.setFont (uiFont (fontSize, active));
            g.drawText (juce::String (z.label.c_str()), r.reduced (4.0f, 0.0f),
                        juce::Justification::centred, false);
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
        const float radius = 22.0f;

        g.setColour (accent.withAlpha (0.75f));
        g.drawEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (p), 1.6f);

        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (p));

        g.setColour (pal::text.withAlpha (0.55f));
        g.drawLine (p.x - radius - 7.0f, p.y, p.x - radius - 2.0f, p.y, 1.2f);
        g.drawLine (p.x + radius + 2.0f, p.y, p.x + radius + 7.0f, p.y, 1.2f);
    }
}

} // namespace sl::ui
