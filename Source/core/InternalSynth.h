#pragma once

#include <array>

namespace sl
{

/** A small instrument for the standalone app, which has no host to send MIDI to.
    A soft saw/triangle poly synth on the melodic channel and a synthesised drum
    kit on the drum channel. The plugin leaves this bypassed. */
class InternalSynth
{
public:
    void prepare (double sampleRate);
    void noteOn (int note, int velocity, int channel);
    void noteOff (int note, int channel);
    void allNotesOff();

    void render (float* left, float* right, int numSamples);

    void setGain (float g) noexcept { gain = g; }
    void setDrumChannel (int ch) noexcept { drumChannel = ch; }

private:
    static constexpr int numVoices = 12;
    static constexpr int numDrums = 10;

    struct Voice
    {
        bool active = false;
        int note = -1;
        int channel = 1;
        float velocity = 0.0f;
        double phase = 0.0;
        double phaseInc = 0.0;
        double phase2 = 0.0;
        float env = 0.0f;
        int stage = 0;          // 0 release, 1 attack, 2 decay, 3 sustain
        float lp = 0.0f;
        float pan = 0.0f;
    };

    struct Drum
    {
        bool active = false;
        int type = 0;
        float velocity = 0.0f;
        float env = 0.0f;
        float decay = 0.99f;
        double phase = 0.0;
        float tone = 0.0f;
        float noise = 0.0f;
        float pan = 0.0f;
    };

    float nextNoise();
    void triggerDrum (int note, float velocity);
    float renderVoice (Voice&, double sampleRate);
    float renderDrum (Drum&, double sampleRate);

    std::array<Voice, numVoices> voices;
    std::array<Drum, numDrums> drums;

    double sr = 48000.0;
    float gain = 0.55f;
    int drumChannel = 10;
    unsigned int rng = 0x1234567u;
};

} // namespace sl
