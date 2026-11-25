// What pangea-render (Main.cpp) and the shell it is built with (platform/HybridShell.cpp, with
// PANGEA_RENDER_TOOL) share for the offline checks of a logic module and a MIDI tool.
#pragma once

#include "PangeaModel.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

namespace pangea::render
{

/** One block of a transport plan: where it starts, how long it is, whether it plays. */
struct Block
{
    double ppq = 0.0;
    double bpm = 120.0;
    double beatsPerBar = 4.0;
    int numSamples = 512;
    bool playing = true;
};

/** One event the module returned on its own, in the block of the plan it came in. */
struct ModuleEvent
{
    int block = 0;
    int offset = 0;
    int control = 0;
    float value = 0.0f;
    int note = 0;
    int channel = 0;
};

/**
 * A control the host sets: its label, what it holds now, its range, and which it is: "bpm" for
 * [pangea:bpm] (decision 55), "playing" for [pangea:playing] and "beat" for [pangea:beat] (decision 123).
 */
struct TempoValue
{
    juce::String label;
    float value = 0.0f, min = 0.0f, max = 0.0f;
    juce::String kind = "bpm";
};

/** One control of an instrument's effect section (decision 73), as the effect check reads it. */
struct EffectControl
{
    juce::String id, key, name;   // the parameter id, the layout key, the name a host shows
    int voiceZones = 0;           // the voices that hold it too: none for a control only the effect has
    int effectZones = 0;          // 1 when the control is bound to the effect section's own zone
    bool parameter = false;       // a person turns it: not the host's tempo, a voice or a logic control
    float min = 0.0f, max = 1.0f;
};

/** Pangea's bypass on a natively drawn panel (decision 75). */
struct PanelBypass
{
    juce::Button* button = nullptr;   // the switch in the title row, or null when the panel has none
    bool controlsFainter = false;     // the controls are drawn fainter, as for a bypassed plugin
};

/** What a processor has sounding, as the bypass check reads it between blocks (decision 75). */
struct Sounding
{
    bool polyphonic = false;      // Faust's polyphony plays it: the voice counts mean something
    int numVoices = 0;            // how many voices it has
    int voicesHolding = 0;        // voices that hold a note
    int voicesBusy = 0;           // voices not yet free: holding a note, or ringing out after one
    int logicNotes = 0;           // notes the logic module has sounding
    bool modulePlaying = false;   // the logic module's last block was run as playing
};
} // namespace pangea::render

/** True when this build is a MIDI tool. */
bool pangeaIsMidiTool();
/** True when this build is an instrument. */
bool pangeaIsInstrument();
/** True when this build carries a logic module. */
bool pangeaHasLogicModule();
/** True when this build's panel is the component library in a web view (build.sh --web-editor). */
bool pangeaHasWebPanel();
/** The voices and the module's notes that sound now. */
pangea::render::Sounding pangeaSounding (juce::AudioProcessor& processor);
/** The bypass switch of a natively drawn panel, and whether the panel draws its controls fainter. */
pangea::render::PanelBypass pangeaPanelBypass (juce::AudioProcessorEditor& editor);
/** The logic module's controls, as the editor and the parameters read them (empty without one). */
std::vector<pangea::LogicControl> pangeaLogicControls();
/** The whole number display slot `slot` of this processor shows, or -1. */
int pangeaDisplayPosition (juce::AudioProcessor& processor, int slot);
/**
 * Runs a fresh copy of the module alone through `plan`, within the step budget; false when the
 * build has no module. `stoppedAt`, when given, is the block where the budget stopped it, or -1.
 */
bool pangeaRunModule (double sampleRate, const std::vector<float>& params, const std::vector<pangea::render::Block>& plan,
                      std::vector<pangea::render::ModuleEvent>& out, int* stoppedAt = nullptr);
/** 0 while the processor's module runs; 1 stopped for running too long, 2 for going too deep, 3 never run. */
int pangeaLogicStopped (juce::AudioProcessor& processor);
/** Each control the host sets (tempo, play state, song position), with its value after the last block. */
std::vector<pangea::render::TempoValue> pangeaTempoValues (juce::AudioProcessor& processor);
/** The layout key of each parameter, in the plugin's order: the site's name for the control. */
juce::StringArray pangeaParameterKeys (juce::AudioProcessor& processor);
/** True when this build runs an effect section after its voices (decision 73). */
bool pangeaHasEffectSection();

/** The sidechain inputs the build's code declares (docs/sidechain.md): 0, 1 or 2. */
int pangeaSidechainInputs (juce::AudioProcessor& processor);

/** The Sidechain bus's channels in the processor's last block: 0 while the bus is off. */
int pangeaSidechainHeard (juce::AudioProcessor& processor);
/** The effect section's controls, in the order the site stores them; none without a section. */
std::vector<pangea::render::EffectControl> pangeaEffectControls (juce::AudioProcessor& processor);
/** The most steps one block of the processor's module has taken. */
long long pangeaMostLogicSteps (juce::AudioProcessor& processor);
/** Shows the stopped message on the processor's editor without stopping anything (for pictures). */
void pangeaShowStopped (juce::AudioProcessor& processor, int why);
namespace pangea { class MeterTaps; }
/** The bar graphs the processor taps for the web panel's meters and step lights (decision 91). */
const pangea::MeterTaps* pangeaMeterTaps (juce::AudioProcessor& processor);
