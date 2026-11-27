#pragma once

#include "Theme.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace sl::ui
{

using APVTS = juce::AudioProcessorValueTreeState;

//==============================================================================
class TabBar : public juce::Component
{
public:
    explicit TabBar (juce::StringArray tabs)
        : labels (std::move (tabs))
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    int getSelected() const noexcept { return selected; }

    void setSelected (int index, bool notify)
    {
        index = juce::jlimit (0, labels.size() - 1, index);

        if (index == selected)
            return;

        selected = index;
        repaint();

        if (notify && onTabChanged)
            onTabChanged (selected);
    }

    std::function<void (int)> onTabChanged;

    void paint (juce::Graphics& g) override
    {
        auto full = getLocalBounds().toFloat();

        g.setColour (pal::panel);
        g.fillRoundedRectangle (full, 12.0f);

        const float w = full.getWidth() / (float) labels.size();

        for (int i = 0; i < labels.size(); ++i)
        {
            auto r = full.withWidth (w).withX (full.getX() + (float) i * w).reduced (3.0f, 4.0f);

            if (i == selected)
            {
                g.setColour (pal::panelHi);
                g.fillRoundedRectangle (r, 9.0f);
                g.setColour (juce::Colours::white);
            }
            else
            {
                g.setColour (pal::dim);
            }

            g.setFont (uiFont (14.0f, i == selected));
            g.drawText (labels[i], r, juce::Justification::centred, false);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const float w = (float) getWidth() / (float) labels.size();
        setSelected (juce::jlimit (0, labels.size() - 1, (int) ((float) e.x / w)), true);
    }

private:
    juce::StringArray labels;
    int selected = 0;
};

//==============================================================================
/** A pill-shaped switch with a highlighted selection, used for the overlay
    choice floating over the stage. */
class SegmentedControl : public juce::Component
{
public:
    SegmentedControl (juce::StringArray items, juce::Colour accentColour)
        : labels (std::move (items)), accent (accentColour)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void setSelected (int index, bool notify)
    {
        index = juce::jlimit (0, labels.size() - 1, index);

        if (index == selected)
            return;

        selected = index;
        repaint();

        if (notify && onChange)
            onChange (selected);
    }

    int getSelected() const noexcept { return selected; }
    void setAccent (juce::Colour c) { accent = c; repaint(); }

    std::function<void (int)> onChange;

    void paint (juce::Graphics& g) override
    {
        auto full = getLocalBounds().toFloat();

        g.setColour (pal::bg.withAlpha (0.72f));
        g.fillRoundedRectangle (full, 9.0f);
        g.setColour (pal::stroke.withAlpha (0.7f));
        g.drawRoundedRectangle (full.reduced (0.5f), 9.0f, 1.0f);

        const float w = full.getWidth() / (float) labels.size();

        for (int i = 0; i < labels.size(); ++i)
        {
            auto r = full.withWidth (w).withX (full.getX() + (float) i * w).reduced (3.0f, 3.0f);
            const bool on = i == selected;

            if (on)
            {
                g.setColour (accent.withAlpha (0.85f));
                g.fillRoundedRectangle (r, 7.0f);
            }

            g.setColour (on ? juce::Colours::black : pal::text.withAlpha (0.75f));
            g.setFont (uiFont (11.0f, true));
            g.drawText (labels[i].toUpperCase(), r, juce::Justification::centred, false);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const float w = (float) getWidth() / (float) labels.size();
        setSelected (juce::jlimit (0, labels.size() - 1, (int) ((float) e.x / w)), true);
    }

private:
    juce::StringArray labels;
    juce::Colour accent;
    int selected = 0;
};

//==============================================================================
/** Caption on the left, thin slider in the middle, value on the right. */
class SliderRow : public juce::Component
{
public:
    SliderRow (juce::String caption, juce::Colour accentColour, juce::String suffix = {})
        : cap (std::move (caption)), suffixText (std::move (suffix))
    {
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        slider.setColour (juce::Slider::thumbColourId, accentColour);
        slider.setColour (juce::Slider::trackColourId, accentColour.withAlpha (0.65f));
        slider.setColour (juce::Slider::backgroundColourId, pal::stroke);
        addAndMakeVisible (slider);
    }

    void attach (APVTS& state, const juce::String& id)
    {
        attachment = std::make_unique<APVTS::SliderAttachment> (state, id, slider);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();

        g.setColour (pal::dim);
        g.setFont (uiFont (11.5f));
        g.drawText (cap, r.removeFromLeft (96).toFloat(), juce::Justification::centredLeft, false);

        g.setFont (uiFont (11.5f, true));
        g.setColour (pal::text.withAlpha (0.75f));
        g.drawText (juce::String (juce::roundToInt (slider.getValue())) + suffixText,
                    r.removeFromRight (52).toFloat(), juce::Justification::centredRight, false);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromLeft (96);
        r.removeFromRight (52);
        r.reduce (0, 8);
        slider.setBounds (r);
    }

    juce::Slider slider;

private:
    juce::String cap, suffixText;
    std::unique_ptr<APVTS::SliderAttachment> attachment;
};

//==============================================================================
class LabelledCombo : public juce::Component
{
public:
    explicit LabelledCombo (juce::String caption, juce::Colour accentColour = pal::teal)
        : cap (std::move (caption)), accent (accentColour)
    {
        addAndMakeVisible (box);
    }

    void attach (APVTS& state, const juce::String& id)
    {
        if (auto* parameter = state.getParameter (id))
        {
            const auto items = parameter->getAllValueStrings();

            if (! items.isEmpty())
            {
                box.clear (juce::dontSendNotification);
                box.addItemList (items, 1);
            }
        }

        attachment = std::make_unique<APVTS::ComboBoxAttachment> (state, id, box);
    }

    void paint (juce::Graphics& g) override
    {
        drawCaption (g, getLocalBounds().removeFromTop (14), cap);
    }

    void resized() override
    {
        box.setBounds (getLocalBounds().withTrimmedTop (16));
    }

    juce::ComboBox box;

private:
    juce::String cap;
    juce::Colour accent;
    std::unique_ptr<APVTS::ComboBoxAttachment> attachment;
};

//==============================================================================
class TogglePill : public juce::Component
{
public:
    TogglePill (const juce::String& text, juce::Colour accent)
    {
        button.setButtonText (text);
        button.setClickingTogglesState (true);
        button.setColour (juce::TextButton::buttonOnColourId, accent);
        addAndMakeVisible (button);
    }

    void attach (APVTS& state, const juce::String& id)
    {
        attachment = std::make_unique<APVTS::ButtonAttachment> (state, id, button);
    }

    void resized() override { button.setBounds (getLocalBounds()); }

    juce::TextButton button;

private:
    std::unique_ptr<APVTS::ButtonAttachment> attachment;
};

} // namespace sl::ui
