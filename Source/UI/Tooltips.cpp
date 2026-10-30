#include "Controls.h"

namespace ui
{
juce::String describe (const juce::String& id)
{
    if (id.endsWith ("_level")) return "Level of this group in the kit.";
    if (id.endsWith ("_tune"))  return "Retunes every drum in this group, in semitones.";
    if (id.endsWith ("_decay")) return "Shortens the ring: 100 % is the natural decay, lower damps it like a hand or tape on the head.";
    if (id.endsWith ("_tone"))  return "Tilts the tone darker (left) or brighter (right).";
    static const std::map<juce::String, juce::String> text {
        { "feel_dynamics", "How much velocity changes the sound: soft hits get quieter, darker and lose stick attack." },
        { "feel_human", "Small differences on every hit (pitch, tone, level, alternate samples) so repeats never sound identical." },
        { "feel_timing", "Random timing drift, early or late, up to this many milliseconds. Leave at 0 with Session Player, which already places hits." },
        { "kick_blend", "Blends the main kick (left) into the second kick (right)." },
        { "kick_sub", "Level of the sub layer under both kicks." },
        { "snare_wires", "Snare wires buzzing in sympathy when the kick and toms are hit." },
        { "hats_close", "How quickly an open hat closes when the pedal or a closed hat is played." },
        { "bus_room", "Amount of room around the kit." },
        { "bus_room_size", "Size of the room." },
        { "bus_glue", "Gentle bus compression that pulls the kit together." },
        { "bus_drive", "Tape-style saturation on the whole kit." },
        { "bus_width", "Stereo width of the kit; the kick stays centred." },
        { "bus_output", "Output level." },
        { "bus_punch", "Lifts the attack of every kick, snare, tom and percussion hit and sits the ring back: more impact." },
        { "bus_smash", "Blends a hard-compressed copy of the kit underneath the dry kit: density and weight without flattening the hits." },
    };
    const auto it = text.find (id);
    return it != text.end() ? it->second : juce::String();
}
} // namespace ui
