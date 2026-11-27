#include "InternalSynth.h"
#include "Scales.h"

#include <algorithm>
#include <cmath>

namespace sl
{

namespace
{
    double noteToHz (int note) { return 440.0 * std::pow (2.0, (note - 69) / 12.0); }

    int drumTypeForNote (int note)
    {
        switch (note)
        {
            case 36: return 0;   // kick
            case 38:
            case 40: return 1;   // snare
            case 42:
            case 44: return 2;   // closed hat
            case 46: return 3;   // open hat
            case 39: return 4;   // clap
            case 45:
            case 47: return 5;   // low tom
            case 49:
            case 57: return 6;   // crash
            case 51:
            case 59: return 7;   // ride
        }

        return 8;
    }
}

void InternalSynth::prepare (double sampleRate)
{
    sr = std::max (8000.0, sampleRate);
    voices = {};
    drums = {};
}

float InternalSynth::nextNoise()
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return (float) ((double) (rng & 0xffffu) / 32768.0 - 1.0);
}

void InternalSynth::noteOn (int note, int velocity, int channel)
{
    if (channel == drumChannel)
    {
        triggerDrum (note, (float) velocity / 127.0f);
        return;
    }

    // Reuse the voice already holding this note, else the oldest free one.
    Voice* slot = nullptr;

    for (auto& v : voices)
        if (v.active && v.note == note && v.channel == channel)
            slot = &v;

    if (slot == nullptr)
        for (auto& v : voices)
            if (! v.active)
            {
                slot = &v;
                break;
            }

    if (slot == nullptr)
    {
        float quietest = 1.0e9f;

        for (auto& v : voices)
            if (v.env < quietest)
            {
                quietest = v.env;
                slot = &v;
            }
    }

    if (slot == nullptr)
        return;

    *slot = {};
    slot->active = true;
    slot->note = note;
    slot->channel = channel;
    slot->velocity = std::clamp ((float) velocity / 127.0f, 0.0f, 1.0f);
    slot->phaseInc = noteToHz (note) / sr;
    slot->stage = 1;
    slot->env = 0.0f;
    slot->pan = std::clamp ((float) (note - 60) / 40.0f, -0.7f, 0.7f);
}

void InternalSynth::noteOff (int note, int channel)
{
    for (auto& v : voices)
        if (v.active && v.note == note && v.channel == channel)
            v.stage = 0;
}

void InternalSynth::allNotesOff()
{
    for (auto& v : voices)
        v.stage = 0;

    for (auto& d : drums)
        d.active = false;
}

void InternalSynth::triggerDrum (int note, float velocity)
{
    Drum* slot = nullptr;

    for (auto& d : drums)
        if (! d.active)
        {
            slot = &d;
            break;
        }

    if (slot == nullptr)
        slot = &drums[0];

    *slot = {};
    slot->active = true;
    slot->type = drumTypeForNote (note);
    slot->velocity = std::clamp (velocity, 0.05f, 1.0f);
    slot->env = 1.0f;
    slot->tone = 0.0f;
    slot->pan = (note == 42 || note == 46) ? 0.25f : (note == 39 ? -0.25f : 0.0f);

    switch (slot->type)
    {
        case 0: slot->decay = 0.9995f; break;  // kick
        case 1: slot->decay = 0.9990f; break;  // snare
        case 2: slot->decay = 0.9950f; break;  // closed hat
        case 3: slot->decay = 0.9993f; break;  // open hat
        case 4: slot->decay = 0.9985f; break;  // clap
        case 5: slot->decay = 0.9992f; break;  // tom
        case 6: slot->decay = 0.99975f; break; // crash
        case 7: slot->decay = 0.9996f; break;  // ride
        default: slot->decay = 0.999f; break;
    }
}

float InternalSynth::renderVoice (Voice& v, double)
{
    if (! v.active)
        return 0.0f;

    const float attack  = 1.0f / (float) (0.006 * sr);
    const float decay   = 1.0f / (float) (0.18 * sr);
    const float release = 1.0f / (float) (0.22 * sr);

    switch (v.stage)
    {
        case 1:
            v.env += attack;

            if (v.env >= 1.0f)
            {
                v.env = 1.0f;
                v.stage = 2;
            }

            break;

        case 2:
            v.env -= decay * 0.65f;

            if (v.env <= 0.72f)
            {
                v.env = 0.72f;
                v.stage = 3;
            }

            break;

        case 3:
            break;

        default:
            v.env -= release;

            if (v.env <= 0.0005f)
            {
                v.active = false;
                return 0.0f;
            }

            break;
    }

    v.phase += v.phaseInc;

    if (v.phase >= 1.0)
        v.phase -= 1.0;

    v.phase2 += v.phaseInc * 2.003;

    if (v.phase2 >= 1.0)
        v.phase2 -= 1.0;

    const float saw = (float) (2.0 * v.phase - 1.0);
    const float tri = (float) (1.0 - 4.0 * std::abs (v.phase - 0.5));
    float s = 0.62f * saw + 0.38f * tri;

    // One-pole low pass that opens with velocity, so soft notes are softer.
    const float cutoff = 0.05f + 0.30f * v.velocity;
    v.lp += (s - v.lp) * cutoff;
    s = v.lp * 0.85f + tri * 0.15f;

    const float fade = std::min (1.0f, v.env * 1.6f);
    return s * v.env * fade * v.velocity * 0.16f;
}

float InternalSynth::renderDrum (Drum& d, double)
{
    if (! d.active)
        return 0.0f;

    const float noise = nextNoise();
    float s = 0.0f;

    switch (d.type)
    {
        case 0:   // kick: pitch-swept sine
        {
            const float f = 48.0f + 90.0f * d.env * d.env;
            d.phase += f / sr;
            s = (float) std::sin (d.phase * 2.0 * 3.14159265358979) * d.env;
            s += noise * 0.04f * d.env * d.env;
            d.decay = 0.9994f;
            break;
        }

        case 1:   // snare: noise + body
        {
            d.phase += 175.0 / sr;
            const float body = (float) std::sin (d.phase * 2.0 * 3.14159265358979) * 0.45f;
            s = (noise * 0.85f + body) * d.env;
            d.decay = 0.9985f;
            break;
        }

        case 2:   // closed hat
        case 3:   // open hat
        {
            d.tone += (noise - d.tone) * 0.55f;
            const float bright = noise - d.tone;
            s = bright * d.env * 0.5f;
            d.decay = d.type == 2 ? 0.9950f : 0.9990f;
            break;
        }

        case 4:   // clap: three quick noise bursts
        {
            d.phase += 1.0;
            const double t = d.phase / sr;
            const float burst = (t < 0.012 || (t > 0.018 && t < 0.030) || (t > 0.036 && t < 0.048)) ? 1.0f : 0.0f;
            s = noise * (0.35f + 0.65f * burst) * d.env * 0.8f;
            d.decay = 0.9988f;
            break;
        }

        case 5:   // tom
        {
            const float f = 92.0f + 55.0f * d.env;
            d.phase += f / sr;
            s = (float) std::sin (d.phase * 2.0 * 3.14159265358979) * d.env * 0.9f;
            d.decay = 0.9990f;
            break;
        }

        case 6:   // crash
        case 7:   // ride
        default:
        {
            d.tone += (noise - d.tone) * 0.25f;
            const float bright = noise - d.tone;
            const float ring = (float) std::sin (d.phase * 2.0 * 3.14159265358979) * 0.08f;
            d.phase += (d.type == 7 ? 780.0 : 320.0) / sr;
            s = (bright * 0.55f + ring) * d.env;
            d.decay = d.type == 7 ? 0.9995f : 0.99972f;
            break;
        }
    }

    d.env *= d.decay;

    if (d.env < 0.0008f)
    {
        d.active = false;
        d.env = 0.0f;
    }

    return s * d.velocity * 0.55f;
}

void InternalSynth::render (float* left, float* right, int numSamples)
{
    if (left == nullptr || right == nullptr)
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        float l = 0.0f, r = 0.0f;

        for (auto& v : voices)
        {
            const float s = renderVoice (v, sr);
            l += s * (0.5f - v.pan * 0.5f);
            r += s * (0.5f + v.pan * 0.5f);
        }

        for (auto& d : drums)
        {
            const float s = renderDrum (d, sr);
            l += s * (0.5f - d.pan * 0.5f);
            r += s * (0.5f + d.pan * 0.5f);
        }

        left[i]  += l * gain * 2.0f;
        right[i] += r * gain * 2.0f;
    }
}

} // namespace sl
