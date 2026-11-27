#include "StandaloneBridge.h"

// The only place in the project that knows about the standalone wrapper.
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

namespace sl::standalone
{

namespace
{
    juce::StandalonePluginHolder* holder()
    {
        return juce::StandalonePluginHolder::getInstance();
    }
}

bool isRunning()
{
    return holder() != nullptr;
}

void showAudioSettings()
{
    if (auto* h = holder())
        h->showAudioSettingsDialog();
}

void unmuteInput()
{
    if (auto* h = holder())
        if ((bool) h->getMuteInputValue().getValue())
            h->getMuteInputValue().setValue (false);
}

void syncMidiOutput()
{
    auto* h = holder();

    if (h == nullptr)
        return;

    static juce::MidiOutput* lastSet = nullptr;
    auto* current = h->deviceManager.getDefaultMidiOutput();

    if (current != lastSet)
    {
        h->player.setMidiOutput (current);
        lastSet = current;
    }
}

juce::String midiOutputName()
{
    if (auto* h = holder())
        if (auto* out = h->deviceManager.getDefaultMidiOutput())
            return out->getName();

    return {};
}

} // namespace sl::standalone
