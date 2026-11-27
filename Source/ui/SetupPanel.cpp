#include "SetupPanel.h"

namespace sl::ui
{

SetupPanel::SetupPanel (SightLineProcessor& p, std::function<void()> onCameraChanged,
                        std::function<void()> onTestNote)
    : proc (p), cameraChanged (std::move (onCameraChanged)), testNote (std::move (onTestNote))
{
    addAndMakeVisible (cameraCombo);
    addAndMakeVisible (mirrorPill);
    addAndMakeVisible (builtInPill);
    addAndMakeVisible (sensitivity);
    addAndMakeVisible (dwell);
    addAndMakeVisible (release);
    addAndMakeVisible (velocity);
    addAndMakeVisible (opacity);
    addAndMakeVisible (volume);
    addAndMakeVisible (melodyChan);
    addAndMakeVisible (drumChan);
    addAndMakeVisible (testButton);

    sensitivity.attach (proc.apvts, "sensitivity");
    dwell.attach (proc.apvts, "dwell");
    release.attach (proc.apvts, "release");
    velocity.attach (proc.apvts, "velocity");
    opacity.attach (proc.apvts, "opacity");
    volume.attach (proc.apvts, "gain");

    melodyChan.attach (proc.apvts, "melodyChan");
    drumChan.attach (proc.apvts, "drumChan");

    mirrorPill.attach (proc.apvts, "mirror");
    builtInPill.attach (proc.apvts, "builtIn");

    testButton.onClick = [this] { if (testNote) testNote(); };
    testButton.setColour (juce::TextButton::buttonColourId, pal::panelHi);

    cameraCombo.onChange = [this]
    {
        if (auto* param = dynamic_cast<juce::AudioParameterInt*> (proc.apvts.getParameter ("cameraIndex")))
            param->setValueNotifyingHost (param->convertTo0to1 ((float) (cameraCombo.getSelectedId() - 1)));

        if (cameraChanged)
            cameraChanged();

        repaint();
    };

    startTimerHz (4);
}

SetupPanel::~SetupPanel() { stopTimer(); }

void SetupPanel::timerCallback()
{
    // The parameter can change without this page (undo, project load).
    const int wanted = proc.cameraIndex() + 1;

    if (cameraCombo.getSelectedId() != wanted && cameraCombo.getNumItems() >= wanted && wanted > 0)
        cameraCombo.setSelectedId (wanted, juce::dontSendNotification);

    repaint();
}

void SetupPanel::setCameraNames (const juce::StringArray& names, int current)
{
    cameraCombo.clear (juce::dontSendNotification);

    if (names.isEmpty())
        cameraCombo.addItem ("No camera found", 1);
    else
        for (int i = 0; i < names.size(); ++i)
            cameraCombo.addItem (names[i], i + 1);

    lastCameraSelection = current;

    if (cameraCombo.getNumItems() > 0)
        cameraCombo.setSelectedId (juce::jlimit (1, cameraCombo.getNumItems(), current + 1),
                                   juce::dontSendNotification);
}

void SetupPanel::setCameraStatus (const juce::String& name, bool live)
{
    const auto text = live ? name + "  ·  live" : juce::String ("NO CAMERA");

    if (text != cameraStatus || live != cameraLive)
    {
        cameraStatus = text;
        cameraLive = live;
        repaint();
    }
}

void SetupPanel::setStandaloneInfo (bool isStandalone, const juce::String& midiOut)
{
    standalone = isStandalone;
    midiStatus = midiOut;
    builtInPill.setVisible (standalone);
    volume.setVisible (standalone);
    resized();
    repaint();
}

void SetupPanel::paint (juce::Graphics& g)
{
    drawPanel (g, cameraCard.toFloat());
    drawPanel (g, playCard.toFloat());
    drawPanel (g, midiCard.toFloat());

    auto c1 = cameraCard, c2 = playCard, c3 = midiCard;

    drawSectionHeader (g, c1.removeFromTop (46).withTrimmedLeft (16), "Camera", pal::teal);
    drawSectionHeader (g, c2.removeFromTop (46).withTrimmedLeft (16), "Playing", pal::amber);
    drawSectionHeader (g, c3.removeFromTop (46).withTrimmedLeft (16), "MIDI out", pal::purple);

    g.setColour (cameraLive ? pal::teal : pal::pink);
    g.setFont (uiFont (10.5f, true));
    g.drawFittedText (cameraStatus.toUpperCase(),
                      cameraCard.withTrimmedTop (204).withTrimmedLeft (16).withHeight (16),
                      juce::Justification::centredLeft, 1);

    g.setColour (pal::dim);
    g.setFont (uiFont (10.5f));
    g.drawFittedText ("The picture never leaves this machine: frames are analysed here and thrown away.",
                      cameraCard.withTrimmedTop (230).reduced (16, 0).withTrimmedBottom (18),
                      juce::Justification::topLeft, 3);

    g.drawFittedText ("Dwell is how long a hand rests on a target before it counts. "
                      "Release is how long the note rings after the hand leaves.",
                      playCard.withTrimmedTop (238).reduced (16, 0).withTrimmedBottom (18),
                      juce::Justification::topLeft, 3);

    juce::String midiText;

    if (standalone)
        midiText = midiStatus.isNotEmpty()
                     ? "MIDI out: " + midiStatus
                     : juce::String ("No MIDI output chosen: the built-in sound plays instead.");
    else
        midiText = "In a DAW, notes go to the instrument on this track. Audio passes through untouched.";

    g.setColour (pal::dim);
    g.setFont (uiFont (10.5f));
    g.drawFittedText (midiText, midiCard.withTrimmedTop (300).reduced (16, 0).withTrimmedBottom (18),
                      juce::Justification::topLeft, 4);
}

void SetupPanel::resized()
{
    auto b = getLocalBounds();
    const int gap = 16;
    const int w = (b.getWidth() - gap * 2) / 3;

    cameraCard = b.removeFromLeft (w);
    b.removeFromLeft (gap);
    playCard = b.removeFromLeft (w);
    b.removeFromLeft (gap);
    midiCard = b;

    // ---- camera ----------------------------------------------------------
    auto c = cameraCard.reduced (16, 0);
    c.removeFromTop (58);
    cameraCombo.setBounds (c.removeFromTop (30));
    c.removeFromTop (14);
    mirrorPill.setBounds (c.removeFromTop (28).withWidth (120));
    c.removeFromTop (12);
    sensitivity.setBounds (c.removeFromTop (30));
    c.removeFromTop (86);   // status + privacy note, drawn in paint

    // ---- playing ---------------------------------------------------------
    auto pArea = playCard.reduced (16, 0);
    pArea.removeFromTop (58);
    dwell.setBounds (pArea.removeFromTop (30));
    pArea.removeFromTop (10);
    release.setBounds (pArea.removeFromTop (30));
    pArea.removeFromTop (10);
    velocity.setBounds (pArea.removeFromTop (30));
    pArea.removeFromTop (10);
    opacity.setBounds (pArea.removeFromTop (30));

    // ---- MIDI ------------------------------------------------------------
    auto m = midiCard.reduced (16, 0);
    m.removeFromTop (58);
    melodyChan.setBounds (m.removeFromTop (58));
    m.removeFromTop (10);
    drumChan.setBounds (m.removeFromTop (58));
    m.removeFromTop (10);
    builtInPill.setBounds (m.removeFromTop (28).withWidth (150));
    m.removeFromTop (8);
    volume.setBounds (m.removeFromTop (30));
    m.removeFromTop (12);
    testButton.setBounds (m.removeFromTop (30).withWidth (160));
}

} // namespace sl::ui
