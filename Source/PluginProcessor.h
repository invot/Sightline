#pragma once

#include "core/GestureEngine.h"
#include "core/HandTracker.h"
#include "core/InternalSynth.h"
#include "core/ZoneMap.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <atomic>

namespace sl
{

/** What the editor reads to draw the overlay. */
struct LiveState
{
    float handX[2] { 0.0f, 0.0f };
    float handY[2] { 0.0f, 0.0f };
    bool handSeen[2] { false, false };
    int activeNote[2] { -1, -1 };
    int hovered[2] { -1, -1 };
    float dwell[2] { 0.0f, 0.0f };
    float fps = 0.0f;
};

class SightLineProcessor : public juce::AudioProcessor
{
public:
    SightLineProcessor();
    ~SightLineProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    bool isBusesLayoutSupported (const BusesLayout&) const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                { return true; }

    const juce::String getName() const override    { return "sightline"; }
    bool acceptsMidi() const override              { return true; }
    bool producesMidi() const override             { return true; }
    bool isMidiEffect() const override             { return false; }
    double getTailLengthSeconds() const override   { return 0.0; }

    int getNumPrograms() override                  { return 1; }
    int getCurrentProgram() override               { return 0; }
    void setCurrentProgram (int) override          {}
    const juce::String getProgramName (int) override { return "Init"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==========================================================================
    juce::AudioProcessorValueTreeState apvts;

    /** The message thread feeds hand positions in; the audio thread only reads
        the queue. */
    void updateHands (const HandSample& left, const HandSample& right, double nowMs);
    void stopTracking();
    void setCameraFps (float fps) noexcept { live.fps = fps; }

    /** Rebuilds the targets when overlay / key / octave changed. */
    void syncZones();
    /** Drops queued notes and releases what is sounding (parameter jumps). */
    void panic();

    /** Plays a note through the MIDI out (velocity < 0 sends the note off).
        Used by the Setup page's test button. */
    void playPreviewNote (int note, int channel, int velocity);

    //==========================================================================
    /** The note each drum pad plays, including any learned overrides. */
    DrumNotes drumNotes() const;
    int  drumNote (int pad) const;
    void setDrumNote (int pad, int note);
    void resetDrumNote (int pad);

    /** Arms a pad: the next note-on reaching the plugin becomes that pad's
        note. The UI polls takeLearnedNote() on the message thread. */
    void startDrumLearn (int pad);
    void cancelDrumLearn();
    int  learningPad() const noexcept { return learnPad.load(); }
    bool takeLearnedNote (int& pad, int& note);

    const std::vector<Zone>& getZones() const noexcept { return gestures.getZones(); }
    LiveState getLiveState() const;

    Overlay  currentOverlay() const;
    int      currentRoot() const;
    ScaleType currentScale() const;
    int      currentOctave() const;
    int      melodyChannel() const;
    int      drumChannel() const;
    double   dwellMs() const;
    double   releaseMs() const;
    float    sensitivity() const;
    float    overlayOpacity() const;
    bool     isMirrored() const;
    bool     builtInSoundOn() const;
    int      cameraIndex() const;

    bool isStandaloneApp() const noexcept { return wrapperType == wrapperType_Standalone; }

    GestureEngine gestures;

private:
    struct EventQueue
    {
        static constexpr int capacity = 512;

        bool push (const NoteEvent& e)
        {
            const auto w = writePos.load (std::memory_order_relaxed);
            const auto r = readPos.load (std::memory_order_acquire);

            if ((uint32_t) (w - r) >= (uint32_t) capacity)
                return false;

            items[(size_t) (w % (uint32_t) capacity)] = e;
            writePos.store (w + 1, std::memory_order_release);
            return true;
        }

        bool pop (NoteEvent& e)
        {
            const auto r = readPos.load (std::memory_order_relaxed);
            const auto w = writePos.load (std::memory_order_acquire);

            if (r == w)
                return false;

            e = items[(size_t) (r % (uint32_t) capacity)];
            readPos.store (r + 1, std::memory_order_release);
            return true;
        }

        std::array<NoteEvent, capacity> items {};
        std::atomic<uint32_t> writePos { 0 }, readPos { 0 };
    };

    void push (const std::vector<NoteEvent>& events);
    void sendAllNotesOff (juce::MidiBuffer& midi, int offset, int channel);
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    struct Params
    {
        std::atomic<float>* overlay = nullptr;
        std::atomic<float>* keyRoot = nullptr;
        std::atomic<float>* scale = nullptr;
        std::atomic<float>* octave = nullptr;
        std::atomic<float>* velocity = nullptr;
        std::atomic<float>* dwell = nullptr;
        std::atomic<float>* release = nullptr;
        std::atomic<float>* sensitivity = nullptr;
        std::atomic<float>* melodyChan = nullptr;
        std::atomic<float>* drumChan = nullptr;
        std::atomic<float>* opacity = nullptr;
        std::atomic<float>* mirror = nullptr;
        std::atomic<float>* builtIn = nullptr;
        std::atomic<float>* camera = nullptr;
        std::atomic<float>* gain = nullptr;
    } p;

    EventQueue queue;
    InternalSynth synth;
    LiveState live;

    std::atomic<int> learnPad { -1 }, learnedPad { -1 }, learnedNote { -1 };
    int lastDrumHash = 0;

    int lastOverlay = -1, lastRoot = -1, lastScale = -1, lastOctave = -1;
    int lastMelodyChan = -1, lastDrumChan = -1;
    bool lastDrums = false;

    bool synthPrepared = false;
    bool builtInWasOn = true;           // audio thread only
    std::vector<float> monoScratch;     // right channel for a mono output bus

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SightLineProcessor)
};

} // namespace sl
