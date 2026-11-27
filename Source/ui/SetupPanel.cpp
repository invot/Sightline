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
    addAndMakeVisible (pinchPill);
    addAndMakeVisible (aimCombo);
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
    pinchPill.attach (proc.apvts, "pinch");
    pinchPill.button.setTooltip ("On: aim at a target, then pinch (or, when aiming with a pointing finger, "
                                 "press the thumb against the index finger) to play it. "
                                 "Off: resting on a target for the dwell time plays it.");
    aimCombo.attach (proc.apvts, "aim");

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
}

void SetupPanel::setCameraNames (const juce::StringArray& names, int current)
{
    cameraCombo.clear (juce::dontSendNotification);

    if (names.isEmpty())
        cameraCombo.addItem ("No camera found", 1);
    else
        for (int i = 0; i < names.size(); ++i)
            cameraCombo.addItem (names[i], i + 1);

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
    // Called once a second by the editor: only relayout when something changed.
    if (standaloneKnown && isStandalone == standalone && midiOut == midiStatus)
        return;

    standaloneKnown = true;
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
    g.drawFittedText (cameraStatus.toUpperCase(), cameraStatusArea, juce::Justification::centredLeft, 1);

    g.setColour (pal::dim);
    g.setFont (uiFont (10.5f));
    g.drawFittedText ("The picture never leaves this machine: frames are analysed here and thrown away.",
                      cameraNoteArea, juce::Justification::topLeft, 3);

    g.drawFittedText ("Thumb + index aims between the two fingertips: pinch to play, open to stop. "
                      "Pointing finger aims with the index tip while you point: press the thumb to the finger to play. "
                      "Dwell is how long a hand rests on a target before it counts. "
                      "Release is how long the note rings after the hand leaves.",
                      playNoteArea, juce::Justification::topLeft, 5);

    juce::String midiText;

    if (standalone)
        midiText = midiStatus.isNotEmpty()
                     ? "MIDI out: " + midiStatus
                     : juce::String ("No MIDI output chosen: the built-in sound plays instead. "
                                     "Pick a device under Audio / MIDI settings.");
    else
        midiText = "In a DAW, notes go to the instrument on this track. Audio passes through untouched.";

    g.drawFittedText (midiText, midiNoteArea, juce::Justification::topLeft, 4);
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
    c.removeFromTop (20);
    cameraStatusArea = c.removeFromTop (16);
    c.removeFromTop (8);
    cameraNoteArea = c.withTrimmedBottom (16);

    // ---- playing ---------------------------------------------------------
    auto pArea = playCard.reduced (16, 0);
    pArea.removeFromTop (58);
    {
        auto row = pArea.removeFromTop (58);
        aimCombo.setBounds (row.removeFromLeft (row.getWidth() / 2 - 6));
        row.removeFromLeft (12);
        pinchPill.setBounds (row.removeFromBottom (30).withWidth (juce::jmin (row.getWidth(), 150)));
    }

    pArea.removeFromTop (12);

    for (auto* row : { &dwell, &release, &velocity, &opacity })
    {
        row->setBounds (pArea.removeFromTop (30));
        pArea.removeFromTop (10);
    }

    pArea.removeFromTop (14);
    playNoteArea = pArea.withTrimmedBottom (16);

    // ---- MIDI ------------------------------------------------------------
    auto m = midiCard.reduced (16, 0);
    m.removeFromTop (58);
    melodyChan.setBounds (m.removeFromTop (58));
    m.removeFromTop (10);
    drumChan.setBounds (m.removeFromTop (58));
    m.removeFromTop (16);

    // The built-in instrument only exists in the standalone app; in a DAW its
    // controls are hidden, so don't leave a hole where they would be.
    if (standalone)
    {
        builtInPill.setBounds (m.removeFromTop (28).withWidth (150));
        m.removeFromTop (8);
        volume.setBounds (m.removeFromTop (30));
        m.removeFromTop (16);
    }

    testButton.setBounds (m.removeFromTop (30).withWidth (160));
    m.removeFromTop (14);
    midiNoteArea = m.withTrimmedBottom (16);
}

} // namespace sl::ui
