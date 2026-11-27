#include "KeyPanel.h"

namespace sl::ui
{

namespace
{
    const char* const pitchNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
}

KeyPanel::KeyPanel (SightLineProcessor& p)
    : proc (p)
{
    addAndMakeVisible (keyCombo);
    addAndMakeVisible (scaleCombo);
    addAndMakeVisible (octaveCombo);

    keyCombo.attach (proc.apvts, "keyRoot");
    scaleCombo.attach (proc.apvts, "scale");

    for (int v = -2; v <= 3; ++v)
        octaveCombo.addItem ((v > 0 ? "+" : "") + juce::String (v), v + 3);

    octaveCombo.onChange = [this]
    {
        if (auto* param = dynamic_cast<juce::AudioParameterInt*> (proc.apvts.getParameter ("octave")))
            param->setValueNotifyingHost (param->convertTo0to1 ((float) (octaveCombo.getSelectedId() - 3)));

        repaint();
    };

    startTimerHz (10);
}

KeyPanel::~KeyPanel() { stopTimer(); }

void KeyPanel::timerCallback()
{
    const int o = proc.currentOctave();

    if (o != lastOctave)
    {
        lastOctave = o;
        octaveCombo.setSelectedId (o + 3, juce::dontSendNotification);
        repaint();
    }
}

void KeyPanel::paint (juce::Graphics& g)
{
    drawPanel (g, keyCard.toFloat());
    drawPanel (g, railCard.toFloat());

    auto keyHeader = keyCard;
    auto railHeader = railCard;
    drawSectionHeader (g, keyHeader.removeFromTop (46).withTrimmedLeft (16), "Key & Scale", pal::purple);
    drawSectionHeader (g, railHeader.removeFromTop (46).withTrimmedLeft (16), "The rails", pal::teal);

    // ---- pitch class map --------------------------------------------------
    drawPitchClassMap (g, chipArea);

    g.setColour (pal::dim);
    g.setFont (uiFont (11.0f));
    g.drawFittedText ("Notes in the key light up. The Scale overlay puts only these on the rails, "
                      "one octave per side, so nothing you play can be out of key.",
                      chipArea.translated (0, 104).withHeight (64), juce::Justification::topLeft, 3);

    // ---- what each rail plays --------------------------------------------
    const auto overlay = proc.currentOverlay();
    const int base = baseNoteForOctave (proc.currentOctave());
    const auto accent = overlay == Overlay::Drums ? pal::pink
                      : overlay == Overlay::Scale ? pal::purple
                                                  : pal::teal;

    auto area = railCard;
    area.removeFromTop (58);

    const int perSide = [&]
    {
        if (overlay == Overlay::Drums)
            return numDrumPads;

        if (overlay == Overlay::Scale)
            return (int) notesInRange (base, base + 11, proc.currentRoot(), proc.currentScale()).size();

        return 12;
    }();

    auto listArea = area.withTrimmedBottom (66);
    const int rows = juce::jmax (1, perSide);
    const float rowH = juce::jlimit (14.0f, 30.0f, (float) (listArea.getHeight() - 26) / (float) rows);

    for (int side = 0; side < 2; ++side)
    {
        auto col = listArea.withWidth (listArea.getWidth() / 2);

        if (side == 1)
            col = col.translated (col.getWidth(), 0);

        auto header = col.removeFromTop (18);

        g.setColour (pal::dim);
        g.setFont (uiFont (10.0f, true));
        g.drawText (side == 0 ? "LEFT RAIL" : "RIGHT RAIL", header.toFloat(),
                    juce::Justification::centredLeft, false);

        g.setColour (pal::panelSoft.withAlpha (0.5f));
        g.fillRoundedRectangle (col.toFloat().reduced (0.0f, 4.0f), 9.0f);

        // One row per target, bottom-up like the overlay itself.
        struct Item { juce::String label; bool accented; };
        juce::Array<Item> items;

        if (overlay == Overlay::Drums)
        {
            for (int i = 0; i < numDrumPads; ++i)
                items.add ({ juce::String (drumPadName (i)) + "   " + juce::String (drumPadNote (i)),
                             i % 2 == 1 });
        }
        else if (overlay == Overlay::Scale)
        {
            for (int n : notesInRange (base + side * 12, base + side * 12 + 11,
                                       proc.currentRoot(), proc.currentScale()))
                items.add ({ juce::String (midiNoteName (n).c_str()), false });
        }
        else
        {
            for (int i = 0; i < 12; ++i)
            {
                const int note = base + side * 12 + i;
                items.add ({ juce::String (midiNoteName (note).c_str()), isBlackKey (note) });
            }
        }

        float y = (float) col.getBottom();

        for (int i = 0; i < items.size(); ++i)
        {
            y -= rowH;
            auto row = juce::Rectangle<float> ((float) col.getX(), y,
                                              (float) col.getWidth(), rowH - 2.0f).reduced (6.0f, 0.0f);

            auto slab = row.reduced (0.0f, 1.0f);

            g.setColour (items[i].accented ? pal::panelSoft.brighter (0.10f) : pal::panelSoft);
            g.fillRoundedRectangle (slab, 5.0f);

            g.setColour (accent.withAlpha (items[i].accented ? 0.45f : 0.20f));
            g.fillRoundedRectangle (slab.withWidth (3.0f).reduced (0.0f, 2.0f), 1.5f);

            g.setColour (pal::text.withAlpha (items[i].accented ? 0.62f : 0.86f));
            g.setFont (uiFont (juce::jlimit (10.0f, 12.0f, rowH * 0.52f)));
            g.drawText (items[i].label, row.withTrimmedLeft (12.0f), juce::Justification::centredLeft, false);
        }
    }

    juce::String explain;

    switch (overlay)
    {
        case Overlay::Keys:
            explain = "Keys: a keyboard standing on its side, one octave per rail. The short black "
                      "keys sit against the inside edge of each rail.";
            break;

        case Overlay::Scale:
            explain = "Scale: " + juce::String (keyName (proc.currentRoot(), proc.currentScale()).c_str())
                    + " only, one octave per rail, so a hand can land anywhere and still be in key.";
            break;

        case Overlay::Drums:
            explain = "Drums: the same eight-piece kit on both rails, sent on channel "
                    + juce::String (proc.drumChannel()) + " with General MIDI note numbers.";
            break;
    }

    g.setColour (pal::dim);
    g.setFont (uiFont (11.0f));
    g.drawFittedText (explain, area.removeFromBottom (58), juce::Justification::topLeft, 3);

    g.setColour (accent.withAlpha (0.7f));
    g.fillRoundedRectangle (railCard.toFloat().removeFromBottom (3.0f), 1.5f);
}

void KeyPanel::drawPitchClassMap (juce::Graphics& g, juce::Rectangle<int> area)
{
    const int root = proc.currentRoot();
    const auto scale = proc.currentScale();
    const auto mask = scaleMask (scale);

    const int cols = 6;
    const int cellW = area.getWidth() / cols;
    const int cellH = 34;

    for (int pc = 0; pc < 12; ++pc)
    {
        auto cell = juce::Rectangle<int> (area.getX() + (pc % cols) * cellW,
                                          area.getY() + (pc / cols) * (cellH + 6),
                                          cellW - 6, cellH);

        const bool member = (mask & (1u << pc)) != 0;
        const bool isRoot = pc == root;

        g.setColour (member ? pal::purple.withAlpha (isRoot ? 0.95f : 0.55f) : pal::panelSoft);
        g.fillRoundedRectangle (cell.toFloat(), 7.0f);

        g.setColour (member ? juce::Colours::black.withAlpha (0.85f) : pal::dim.withAlpha (0.7f));
        g.setFont (uiFont (11.5f, member));
        g.drawText (pitchNames[pc], cell, juce::Justification::centred, false);
    }
}

void KeyPanel::resized()
{
    auto b = getLocalBounds();

    keyCard = b.removeFromLeft (392);
    b.removeFromLeft (16);
    railCard = b;

    auto left = keyCard.reduced (16, 0);
    left.removeFromTop (58);

    auto row1 = left.removeFromTop (58);
    keyCombo.setBounds (row1.removeFromLeft (row1.getWidth() / 2 - 6).withTrimmedTop (0));
    scaleCombo.setBounds (row1.withTrimmedLeft (12));

    left.removeFromTop (10);
    auto row2 = left.removeFromTop (58);
    octaveCombo.setBounds (row2.removeFromLeft (120).withTrimmedTop (18));

    left.removeFromTop (18);
    chipArea = left.removeFromTop (86);
}

} // namespace sl::ui
