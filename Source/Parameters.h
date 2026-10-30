#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// Stable parameter IDs: never rename or reuse once shipped.
namespace PID
{
    // per group: level / tune / decay / tone
    inline constexpr const char* groupNames[6] { "kick", "snare", "hats", "toms", "cymbals", "perc" };
    inline juce::String level (int g) { return juce::String (groupNames[g]) + "_level"; }
    inline juce::String tune  (int g) { return juce::String (groupNames[g]) + "_tune"; }
    inline juce::String decay (int g) { return juce::String (groupNames[g]) + "_decay"; }
    inline juce::String tone  (int g) { return juce::String (groupNames[g]) + "_tone"; }

    // feel
    inline constexpr auto dynamics = "feel_dynamics", human = "feel_human", timing = "feel_timing",
                          kickBlend = "kick_blend", sub = "kick_sub", wires = "snare_wires", hatClose = "hats_close";
    // bus
    inline constexpr auto room = "bus_room", roomSize = "bus_room_size", glue = "bus_glue", drive = "bus_drive",
                          width = "bus_width", output = "bus_output", punch = "bus_punch", smash = "bus_smash";
}

enum Group { Kick, Snare, Hats, Toms, Cymbals, Perc, NumGroups };

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
