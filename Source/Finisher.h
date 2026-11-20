#pragma once
// Kolot — finishing stage: stereo/mono, mic model, room, low-cut,
// brightness, reverb, output gain. Fast, so it re-runs on every tweak.

#include "KolotTypes.h"

namespace kolot
{
// Renders the final stereo choir from the stems. Returns false if cancelled.
bool finishChoir (const ChoirStems& stems, const FinishSettings& fs,
                  juce::AudioBuffer<float>& outStereo, const std::atomic<bool>* cancel = nullptr);
}
