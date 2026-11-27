#pragma once

#include <juce_core/juce_core.h>

namespace sl::standalone
{

// The standalone app owns its audio device; a plugin's input is chosen by the
// host instead. These are no-ops (and the statuses are empty) inside a DAW.

bool isRunning();
void showAudioSettings();

/** JUCE's standalone wrapper mutes the audio input by default to avoid a
    feedback loop. SightLine never passes the input to the output, so there is
    no loop: switch the mute off. */
void unmuteInput();

/** The standalone player only picks up the MIDI output device when the audio
    device starts; call this periodically so choosing one takes effect at once. */
void syncMidiOutput();

/** Name of the MIDI output device in use, empty if none (or in a DAW). */
juce::String midiOutputName();

} // namespace sl::standalone
