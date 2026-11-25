// The Pangea plugin's entry point (docs/pangea-plugin.md). Everything is in PangeaHost.h;
// CMake builds this file three times, once per plugin, with PANGEA_HOST_KIND 0 (Pangea, an
// effect), 1 (Pangea Instrument, which also opens MIDI tools) or 2 (Pangea MIDI FX, for Logic).
#include "PangeaHost.h"

#ifndef PANGEA_HOST_KIND
 #define PANGEA_HOST_KIND 0
#endif

// poly-dsp.h brings in Faust's GUI.h, which leaves these two statics to the architecture file.
std::list<GUI*> GUI::fGuiList;
ztimedmap GUI::gTimedZoneMap;

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new pangea::host::Processor (static_cast<pangea::host::HostKind> (PANGEA_HOST_KIND));
}
