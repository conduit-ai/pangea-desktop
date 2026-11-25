// Where a panel finds the parameters behind its controls: a plugin's own parameter state, or, in the
// Pangea plugin (PangeaHost.h), the slot that the loaded version gave each control. Both panels use
// it: the native one (PangeaEditor.h) and the web view (PangeaWebEditor.h).
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>

namespace pangea
{

struct ParamSource
{
    using Find = std::function<juce::RangedAudioParameter* (const juce::String& id)>;

    ParamSource (juce::AudioProcessorValueTreeState& s)   // NOLINT: converts, so a plugin passes its apvts as before
        : find ([&s] (const juce::String& id) { return s.getParameter (id); }), undoManager (s.undoManager) {}
    explicit ParamSource (Find f, juce::UndoManager* undo = nullptr) : find (std::move (f)), undoManager (undo) {}

    juce::RangedAudioParameter* getParameter (const juce::String& id) const { return find ? find (id) : nullptr; }

    /** A parameter's value in its own units (what a plugin's raw value holds). */
    static float realValue (const juce::RangedAudioParameter& p) { return p.convertFrom0to1 (p.getValue()); }

    Find find;
    juce::UndoManager* undoManager = nullptr;
};

} // namespace pangea
