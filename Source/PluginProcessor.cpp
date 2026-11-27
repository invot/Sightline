#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace sl
{

juce::AudioProcessorEditor* SightLineProcessor::createEditor()
{
    return new SightLineEditor (*this);
}

namespace
{
    juce::String padNoteKey (int pad)
    {
        return "padNote" + juce::String (pad);
    }

    juce::StringArray rootNames()
    {
        juce::StringArray a;

        for (int pc = 0; pc < numPitchClasses; ++pc)
            a.add (pitchClassName (pc));

        return a;
    }
}

SightLineProcessor::SightLineProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "sightline", createLayout())
{
    p.overlay     = apvts.getRawParameterValue ("overlay");
    p.keyRoot     = apvts.getRawParameterValue ("keyRoot");
    p.scale       = apvts.getRawParameterValue ("scale");
    p.octave      = apvts.getRawParameterValue ("octave");
    p.velocity    = apvts.getRawParameterValue ("velocity");
    p.dwell       = apvts.getRawParameterValue ("dwell");
    p.release     = apvts.getRawParameterValue ("release");
    p.sensitivity = apvts.getRawParameterValue ("sensitivity");
    p.melodyChan  = apvts.getRawParameterValue ("melodyChan");
    p.drumChan    = apvts.getRawParameterValue ("drumChan");
    p.opacity     = apvts.getRawParameterValue ("opacity");
    p.mirror      = apvts.getRawParameterValue ("mirror");
    p.builtIn     = apvts.getRawParameterValue ("builtIn");
    p.camera      = apvts.getRawParameterValue ("cameraIndex");
    p.gain        = apvts.getRawParameterValue ("gain");
    p.pinch       = apvts.getRawParameterValue ("pinch");
    p.aim         = apvts.getRawParameterValue ("aim");

    syncZones();
}

SightLineProcessor::~SightLineProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout SightLineProcessor::createLayout()
{
    using namespace juce;

    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "overlay", 1 }, "Overlay",
        StringArray { "Keys", "Scale", "Drums" }, 0));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "keyRoot", 1 }, "Key",
        rootNames(), 0));

    StringArray scales;

    for (int i = 0; i < (int) ScaleType::NumScales; ++i)
        scales.add (scaleName ((ScaleType) i));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "scale", 1 }, "Scale", scales, 1));

    layout.add (std::make_unique<AudioParameterInt> (
        ParameterID { "octave", 1 }, "Octave", -2, 3, 0));

    layout.add (std::make_unique<AudioParameterInt> (
        ParameterID { "velocity", 1 }, "Velocity", 1, 127, 100));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "dwell", 1 }, "Dwell",
        NormalisableRange<float> (0.0f, 400.0f, 1.0f), 90.0f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "release", 1 }, "Release",
        NormalisableRange<float> (20.0f, 600.0f, 1.0f), 150.0f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "sensitivity", 1 }, "Sensitivity",
        NormalisableRange<float> (0.0f, 100.0f, 1.0f), 50.0f));

    StringArray channels;

    for (int i = 1; i <= 16; ++i)
        channels.add (String (i));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "melodyChan", 1 }, "Melody channel", channels, 0));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "drumChan", 1 }, "Drum channel", channels, 9));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "opacity", 1 }, "Overlay opacity",
        NormalisableRange<float> (10.0f, 75.0f, 1.0f), 34.0f));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "mirror", 1 }, "Mirror camera", true));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "builtIn", 1 }, "Built-in sound", true));

    layout.add (std::make_unique<AudioParameterInt> (
        ParameterID { "cameraIndex", 1 }, "Camera", 0, 7, 0));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "gain", 1 }, "Built-in volume",
        NormalisableRange<float> (0.0f, 100.0f, 1.0f), 65.0f));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "pinch", 1 }, "Pinch to play", true));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "aim", 1 }, "Aim with",
        StringArray { "Thumb + index", "Pointing finger" }, 0));

    return layout;
}

//==============================================================================
void SightLineProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    synth.prepare (sampleRate);
    monoScratch.assign ((size_t) juce::jmax (1, samplesPerBlock), 0.0f);
    synthPrepared = true;
    queue.readPos.store (queue.writePos.load());
}

void SightLineProcessor::releaseResources()
{
    synth.allNotesOff();
    synthPrepared = false;
}

void SightLineProcessor::reset()
{
    panic();
    synth.allNotesOff();
}

bool SightLineProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    const auto& in = layouts.getMainInputChannelSet();
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo()
        || in == juce::AudioChannelSet::disabled();
}

//==============================================================================
Overlay SightLineProcessor::currentOverlay() const
{
    return (Overlay) juce::jlimit (0, 2, (int) p.overlay->load());
}

int SightLineProcessor::currentRoot() const
{
    return juce::jlimit (0, 11, (int) p.keyRoot->load());
}

ScaleType SightLineProcessor::currentScale() const
{
    return (ScaleType) juce::jlimit (0, (int) ScaleType::NumScales - 1, (int) p.scale->load());
}

int SightLineProcessor::currentOctave() const
{
    return juce::jlimit (-2, 3, (int) p.octave->load());
}

int SightLineProcessor::melodyChannel() const
{
    return 1 + juce::jlimit (0, 15, (int) p.melodyChan->load());
}

int SightLineProcessor::drumChannel() const
{
    return 1 + juce::jlimit (0, 15, (int) p.drumChan->load());
}

double SightLineProcessor::dwellMs() const     { return (double) p.dwell->load(); }
double SightLineProcessor::releaseMs() const   { return (double) p.release->load(); }
float  SightLineProcessor::sensitivity() const { return p.sensitivity->load() / 100.0f; }
float  SightLineProcessor::overlayOpacity() const { return p.opacity->load() / 100.0f; }
bool   SightLineProcessor::isMirrored() const  { return p.mirror->load() > 0.5f; }
bool   SightLineProcessor::pinchToPlay() const { return p.pinch->load() > 0.5f; }
AimMode SightLineProcessor::aimMode() const    { return p.aim->load() > 0.5f ? AimMode::Fingertip : AimMode::PinchPoint; }
bool   SightLineProcessor::builtInSoundOn() const { return p.builtIn->load() > 0.5f; }
int    SightLineProcessor::cameraIndex() const { return (int) p.camera->load(); }

//==============================================================================
DrumNotes SightLineProcessor::drumNotes() const
{
    auto notes = defaultDrumNotes();

    for (int i = 0; i < numDrumPads; ++i)
    {
        const auto value = apvts.state.getProperty (padNoteKey (i));

        if (! value.isVoid())
            notes[(size_t) i] = juce::jlimit (0, 127, (int) value);
    }

    return notes;
}

int SightLineProcessor::drumNote (int pad) const
{
    return drumNotes()[(size_t) juce::jlimit (0, numDrumPads - 1, pad)];
}

void SightLineProcessor::setDrumNote (int pad, int note)
{
    if (pad < 0 || pad >= numDrumPads)
        return;

    note = juce::jlimit (0, 127, note);
    const auto key = padNoteKey (pad);

    // A pad back on its GM note needs no stored value.
    if (note == defaultDrumNotes()[(size_t) pad])
        apvts.state.removeProperty (key, nullptr);
    else
        apvts.state.setProperty (key, note, nullptr);

    // The mapping is not a parameter, so tell the host the document changed.
    updateHostDisplay (juce::AudioProcessorListener::ChangeDetails{}
                           .withNonParameterStateChanged (true));

    syncZones();
}

void SightLineProcessor::resetDrumNote (int pad)
{
    if (pad < 0 || pad >= numDrumPads)
        return;

    apvts.state.removeProperty (padNoteKey (pad), nullptr);
    updateHostDisplay (juce::AudioProcessorListener::ChangeDetails{}
                           .withNonParameterStateChanged (true));
    syncZones();
}

void SightLineProcessor::startDrumLearn (int pad)
{
    if (pad < 0 || pad >= numDrumPads)
        return;

    learnedPad.store (-1);
    learnedNote.store (-1);
    learnPad.store (pad);
}

void SightLineProcessor::cancelDrumLearn()
{
    learnPad.store (-1);
    learnedPad.store (-1);
    learnedNote.store (-1);
}

bool SightLineProcessor::takeLearnedNote (int& pad, int& note)
{
    const int learned = learnedPad.load();

    if (learned < 0)
        return false;

    pad = learned;
    note = learnedNote.load();
    learnedPad.store (-1);
    return true;
}

//==============================================================================
void SightLineProcessor::syncZones()
{
    const auto overlay = currentOverlay();
    const int root = currentRoot();
    const auto scale = currentScale();
    const int octave = currentOctave();
    const int vel = juce::jlimit (1, 127, (int) p.velocity->load());
    const int mel = melodyChannel();
    const int drum = drumChannel();
    const double dwell = dwellMs();
    const double release = releaseMs();
    const bool drums = overlay == Overlay::Drums;
    const auto mapping = drumNotes();

    int mappingHash = 0;

    for (const auto n : mapping)
        mappingHash = mappingHash * 131 + n;

    const bool geometryChanged = (int) overlay != lastOverlay || root != lastRoot
                              || (int) scale != lastScale || octave != lastOctave;
    const bool mappingChanged = mappingHash != lastDrumHash;

    if (geometryChanged || mappingChanged
        || lastMelodyChan != mel || lastDrumChan != drum || lastDrums != drums)
    {
        // Anything sounding belongs to the old layout.
        std::vector<NoteEvent> released;
        gestures.reset (released);
        push (released);

        if (geometryChanged || mappingChanged)
            gestures.setZones (buildZones (overlay, root, scale, octave, mapping).zones);
    }

    gestures.setParams (vel, mel, drum, dwell, release, drums);
    gestures.setPinchToPlay (pinchToPlay());

    lastOverlay = (int) overlay;
    lastRoot = root;
    lastScale = (int) scale;
    lastOctave = octave;
    lastMelodyChan = mel;
    lastDrumChan = drum;
    lastDrums = drums;
    lastDrumHash = mappingHash;
}

void SightLineProcessor::push (const std::vector<NoteEvent>& events)
{
    for (const auto& e : events)
        queue.push (e);
}

void SightLineProcessor::updateHands (const HandSample& left, const HandSample& right, double nowMs)
{
    std::vector<NoteEvent> events;
    events.reserve (8);

    gestures.update (left, right, nowMs, events);
    push (events);

    for (int s = 0; s < 2; ++s)
    {
        const auto& h = s == 0 ? left : right;

        live.handSeen[s] = h.detected;
        live.hasPinch[s] = h.detected && h.hasPinch;
        live.pinching[s] = h.detected && h.pinching;
        live.pointing[s] = h.detected && h.pointing;
        live.hovering[s] = gestures.isHovering (s);
        live.handX[s] = h.x;
        live.handY[s] = h.y;
        live.dwell[s] = gestures.dwellProgress (s, nowMs);

        const int active = gestures.activeZone (s);
        live.activeNote[s] = active >= 0 ? gestures.getZones()[(size_t) active].note : -1;
        live.hovered[s] = gestures.hoveredZone (s);
    }
}

void SightLineProcessor::stopTracking()
{
    panic();

    for (int s = 0; s < 2; ++s)
    {
        live.handSeen[s] = false;
        live.activeNote[s] = -1;
        live.hovered[s] = -1;
        live.dwell[s] = 0.0f;
    }
}

void SightLineProcessor::panic()
{
    std::vector<NoteEvent> events;
    gestures.reset (events);
    push (events);
    queue.push (NoteEvent::panic());

    for (int s = 0; s < 2; ++s)
    {
        live.activeNote[s] = -1;
        live.hovered[s] = -1;
    }
}

LiveState SightLineProcessor::getLiveState() const
{
    LiveState copy = live;
    copy.fps = live.fps;
    return copy;
}

//==============================================================================
void SightLineProcessor::sendAllNotesOff (juce::MidiBuffer& midi, int offset, int channel)
{
    // Only the channels sightline plays on: the rest of the track's MIDI is
    // none of its business.
    midi.addEvent (juce::MidiMessage::allNotesOff (channel), offset);
    midi.addEvent (juce::MidiMessage::allSoundOff (channel), offset);
}

void SightLineProcessor::playPreviewNote (int note, int channel, int velocity)
{
    if (velocity < 0)
        queue.push (NoteEvent::off (note, juce::jlimit (1, 16, channel)));
    else
        queue.push (NoteEvent::on (note, juce::jlimit (1, 127, velocity), juce::jlimit (1, 16, channel)));
}

void SightLineProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // MIDI learn: the next note that reaches the plugin while a pad is armed
    // becomes that pad's note. Scan before sightline's own notes are added, so
    // it cannot learn a note it just played itself. The audio thread only
    // publishes the result; the editor applies and stores it.
    if (const int learning = learnPad.load(); learning >= 0)
    {
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();

            if (m.isNoteOn())
            {
                // Only the call that actually disarms the pad may publish a
                // note: a cancel (or a new learn) that landed in between wins.
                int expected = learning;

                if (learnPad.compare_exchange_strong (expected, -1))
                {
                    learnedNote.store (m.getNoteNumber());
                    learnedPad.store (learning);
                }

                break;
            }
        }
    }

    const bool standalone = isStandaloneApp();
    const int numSamples = buffer.getNumSamples();

    if (standalone)
    {
        // The standalone app has no host instrument: play its own kit instead.
        buffer.clear();
        synth.setDrumChannel (drumChannel());
        synth.setGain (p.gain->load() / 100.0f);

        // Switching the built-in sound off must also silence what it is
        // holding: its note-offs are skipped from now on.
        const bool builtInOn = builtInSoundOn();

        if (builtInWasOn && ! builtInOn)
            synth.allNotesOff();

        builtInWasOn = builtInOn;
    }
    // In a DAW the plugin is transparent: whatever reaches it leaves untouched.

    NoteEvent e;

    while (queue.pop (e))
    {
        switch (e.type)
        {
            case NoteEvent::noteOn:
                midi.addEvent (juce::MidiMessage::noteOn (e.channel, e.note, (juce::uint8) e.velocity), 0);

                if (standalone && builtInSoundOn())
                    synth.noteOn (e.note, e.velocity, e.channel);

                break;

            case NoteEvent::noteOff:
                midi.addEvent (juce::MidiMessage::noteOff (e.channel, e.note), 0);

                if (standalone && builtInSoundOn())
                    synth.noteOff (e.note, e.channel);

                break;

            case NoteEvent::allNotesOff:
            default:
                sendAllNotesOff (midi, 0, melodyChannel());

                if (drumChannel() != melodyChannel())
                    sendAllNotesOff (midi, 0, drumChannel());

                if (standalone)
                    synth.allNotesOff();

                break;
        }
    }

    if (standalone && synthPrepared)
    {
        if (buffer.getNumChannels() > 0)
        {
            buffer.clear();

            const int n = juce::jmin (numSamples, buffer.getNumSamples());

            if (buffer.getNumChannels() >= 2)
            {
                synth.render (buffer.getWritePointer (0), buffer.getWritePointer (1), n);
            }
            else
            {
                // Mono bus: render into a scratch right channel (sized in
                // prepareToPlay, so nothing is allocated here) and fold it in.
                if (monoScratch.size() < (size_t) n)
                    return;

                std::fill_n (monoScratch.begin(), n, 0.0f);
                auto* left = buffer.getWritePointer (0);
                synth.render (left, monoScratch.data(), n);

                for (int i = 0; i < n; ++i)
                    left[i] += monoScratch[(size_t) i];
            }
        }
    }
}

//==============================================================================
void SightLineProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SightLineProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // A project load takes over from any pad that was waiting for a note.
    cancelDrumLearn();

    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));

    syncZones();
}

} // namespace sl

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new sl::SightLineProcessor();
}
