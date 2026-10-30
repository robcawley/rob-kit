#include "Parameters.h"

using namespace juce;
using APF = AudioParameterFloat;

namespace
{
    ParameterID id (const String& s) { return { s, 1 }; }

    String num (float v) { return String (v, std::abs (v) < 10.0f ? 1 : 0); }
    float parse (const String& t, float fallback)
    {
        const auto s = t.trim().retainCharacters ("+-0123456789.");
        return s.isEmpty() ? fallback : s.getFloatValue();
    }

    AudioParameterFloatAttributes pct()
    {
        return AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; })
            .withValueFromStringFunction ([] (const String& t) { return parse (t, 0.0f) / 100.0f; });
    }
    AudioParameterFloatAttributes bipct()
    {
        return AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { const int n = roundToInt (v * 100.0f); return (n > 0 ? "+" : "") + String (n) + " %"; })
            .withValueFromStringFunction ([] (const String& t) { return parse (t, 0.0f) / 100.0f; });
    }
    AudioParameterFloatAttributes db()
    {
        return AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return v <= -47.9f ? String ("-inf") : (v > 0.0f ? "+" : "") + num (v) + " dB"; })
            .withValueFromStringFunction ([] (const String& t) { return t.containsIgnoreCase ("inf") ? -48.0f : parse (t, 0.0f); });
    }
    AudioParameterFloatAttributes semis()
    {
        return AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { const float r = std::round (v * 10.0f) / 10.0f; return (r > 0.0f ? "+" : "") + String (r, 1) + " st"; })
            .withValueFromStringFunction ([] (const String& t) { return parse (t, 0.0f); });
    }
    AudioParameterFloatAttributes ms()
    {
        return AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " ms"; })
            .withValueFromStringFunction ([] (const String& t) { return parse (t, 0.0f); });
    }
    NormalisableRange<float> skewed (float lo, float hi, float centre)
    {
        NormalisableRange<float> r (lo, hi); r.setSkewForCentre (centre); return r;
    }
}

AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    AudioProcessorValueTreeState::ParameterLayout l;
    const char* titles[] { "Kick", "Snare", "Hats", "Toms", "Cymbals", "Perc" };
    const float defLevel[] { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -2.0f };
    for (int g = 0; g < NumGroups; ++g)
    {
        auto grp = std::make_unique<AudioProcessorParameterGroup> (PID::groupNames[g], titles[g], " | ");
        const String t (titles[g]);
        grp->addChild (std::make_unique<APF> (id (PID::level (g)), t + " Level", skewed (-48.0f, 6.0f, -12.0f), defLevel[g], db()));
        grp->addChild (std::make_unique<APF> (id (PID::tune (g)), t + " Tune", NormalisableRange<float> (-6.0f, 6.0f), 0.0f, semis()));
        grp->addChild (std::make_unique<APF> (id (PID::decay (g)), t + " Decay", NormalisableRange<float> (0.05f, 1.0f), 1.0f, pct()));
        grp->addChild (std::make_unique<APF> (id (PID::tone (g)), t + " Tone", NormalisableRange<float> (-1.0f, 1.0f), 0.0f, bipct()));
        l.add (std::move (grp));
    }
    l.add (std::make_unique<AudioProcessorParameterGroup> ("feel", "Feel", " | ",
        std::make_unique<APF> (id (PID::dynamics), "Dynamics", NormalisableRange<float> (0.0f, 1.0f), 0.6f, pct()),
        std::make_unique<APF> (id (PID::human), "Human", NormalisableRange<float> (0.0f, 1.0f), 0.5f, pct()),
        std::make_unique<APF> (id (PID::timing), "Timing Drift", NormalisableRange<float> (0.0f, 3.0f), 0.0f, ms()),
        std::make_unique<APF> (id (PID::kickBlend), "Kick Blend", NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct()),
        std::make_unique<APF> (id (PID::sub), "Sub Kick", NormalisableRange<float> (0.0f, 1.0f), 0.5f, pct()),
        std::make_unique<APF> (id (PID::wires), "Snare Wires", NormalisableRange<float> (0.0f, 1.0f), 0.3f, pct()),
        std::make_unique<APF> (id (PID::hatClose), "Hat Close Time", NormalisableRange<float> (8.0f, 150.0f), 45.0f, ms())));
    l.add (std::make_unique<AudioProcessorParameterGroup> ("bus", "Bus", " | ",
        std::make_unique<APF> (id (PID::room), "Room", NormalisableRange<float> (0.0f, 1.0f), 0.2f, pct()),
        std::make_unique<APF> (id (PID::roomSize), "Room Size", NormalisableRange<float> (0.0f, 1.0f), 0.35f, pct()),
        std::make_unique<APF> (id (PID::glue), "Glue", NormalisableRange<float> (0.0f, 1.0f), 0.25f, pct()),
        std::make_unique<APF> (id (PID::drive), "Drive", NormalisableRange<float> (0.0f, 1.0f), 0.1f, pct()),
        std::make_unique<APF> (id (PID::width), "Width", NormalisableRange<float> (0.0f, 1.5f), 1.0f, pct()),
        std::make_unique<APF> (id (PID::output), "Output", NormalisableRange<float> (-24.0f, 6.0f), 0.0f, db()),
        std::make_unique<APF> (id (PID::punch), "Punch", NormalisableRange<float> (0.0f, 1.0f), 0.45f, pct()),
        std::make_unique<APF> (id (PID::smash), "Smash", NormalisableRange<float> (0.0f, 1.0f), 0.3f, pct())));
    return l;
}
