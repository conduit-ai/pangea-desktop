// The sidechain input (docs/sidechain.md), in plain C++ with no JUCE, so that the bare compiler
// tests it (tools/test-platform.cpp). An effect whose Faust code declares a sidechain,
// `declare sidechain "stereo";` or `declare sidechain "mono";`, takes the sound on its first two
// inputs and the sidechain on the one or two after them. The plugin built from it has a second input
// bus, Sidechain, off until the host routes something to it; the Pangea plugin's effect always has
// one. The site's shape check (src/lib/faust/sidechain.ts) refuses any other value or count before a
// version is saved; here anything else simply has no sidechain.
#pragma once

#include <algorithm>
#include <string>

namespace pangea
{

/** The bus's name, as a DAW shows it beside the plugin. */
inline constexpr const char* sidechainBusName = "Sidechain";

/** What `declare sidechain` says, as inputs after the sound's two: 2, 1, or 0 for anything else. */
inline int sidechainChannelsFor (const std::string& declared)
{
    const auto first = declared.find_first_not_of (" \t");
    const auto last = declared.find_last_not_of (" \t");
    const auto value = first == std::string::npos ? std::string() : declared.substr (first, last - first + 1);
    return value == "stereo" ? 2 : (value == "mono" ? 1 : 0);
}

/** The code's sidechain inputs: what it declares, when its inputs are the sound's two and then exactly those. */
inline int sidechainInputsOf (int declared, int faustInputs)
{
    return declared > 0 && faustInputs == 2 + declared ? declared : 0;
}

/**
 * Fills `count` sidechain inputs of the Faust code, each `n` samples, from the host's sidechain bus:
 * its channels as they are, a mono bus to both, a stereo bus to a mono input as the mean of its two
 * sides, and silence where the bus is off. `bus` is null or holds `busChannels` channels.
 */
inline void fillSidechain (float* const* faustInputs, int count, const float* const* bus, int busChannels, int n)
{
    for (int ch = 0; ch < count; ++ch)
    {
        float* out = faustInputs[ch];
        if (bus == nullptr || busChannels <= 0)
            std::fill (out, out + n, 0.0f);
        else if (count == 1 && busChannels >= 2)
            for (int i = 0; i < n; ++i)
                out[i] = (bus[0][i] + bus[1][i]) * 0.5f;
        else
        {
            const float* in = bus[std::min (ch, busChannels - 1)];
            std::copy (in, in + n, out);
        }
    }
}

} // namespace pangea
