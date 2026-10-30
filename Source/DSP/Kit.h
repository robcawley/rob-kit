#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <atomic>
#include "../Parameters.h"

namespace rk
{
struct Sample
{
    juce::AudioBuffer<float> buf;      // stereo
    double rate = 48000.0;
    juce::String name;
};

enum class Pick { Single, RoundRobin, ByVelocity, Layered };

struct Articulation
{
    juce::String name;
    Group group = Kick;
    Pick pick = Pick::Single;
    std::vector<const Sample*> samples;    // Single: [0]; RoundRobin/ByVelocity: [soft/alt, hard/main]; Layered: one per layer
    std::vector<int> velHi;                // Layered: upper velocity of each layer
    float bend = 0.0f;                     // head pitch bend at full velocity, semitones
    float brightOct = 2.0f;                // octaves darker at the softest hit
    float transient = 0.5f;                // how much a soft hit loses its stick attack
    float detuneCents = 6.0f;              // humanise range
    int chokeGroup = 0;                    // this voice can be choked by...
    int chokes = 0;                        // ...and this hit chokes these groups
    float room = 0.6f;
    int maxVoices = 4;
    bool dampsSelf = true;                 // a new hit damps the previous one (drum heads)
};

// Everything the audio thread needs from the parameters, read once per block.
struct Settings
{
    std::array<float, NumGroups> levelDb {}, tune {}, decay {}, tone {};
    float dynamics = 0.6f, human = 0.5f, timingMs = 0.0f, kickBlend = 0.0f, sub = 0.5f, wires = 0.3f, hatCloseMs = 45.0f;
    float room = 0.2f, roomSize = 0.35f, glue = 0.25f, drive = 0.1f, width = 1.0f, outputDb = 0.0f, punch = 0.45f, smash = 0.3f;
};

struct Telemetry
{
    std::array<std::atomic<uint32_t>, 64> hits {};      // per articulation: increments on each hit
    std::array<std::atomic<float>, 64> lastVel {};
    std::atomic<float> peak[2] {};
    std::atomic<int> voices { 0 };
};

class Kit
{
public:
    static constexpr int kMaxVoices = 72;

    // Loads every sample from 'root' (Samples/Rob, Samples/Fill). Message thread only.
    bool load (const juce::File& root, juce::String& error);
    const std::vector<Articulation>& articulations() const { return arts; }
    int articulationForNote (int note) const { return juce::isPositiveAndBelow (note, 128) ? noteMap[(size_t) note] : -1; }

    void prepare (double sampleRate, int maxBlock);
    int latencySamples() const { return latency; }
    void setSettings (const Settings& s) { set = s; }
    void noteOn (int note, float velocity, int sampleOffset);
    void allSoundOff();
    void render (juce::AudioBuffer<float>& out, int start, int num);   // voices into group buses
    void finish (juce::AudioBuffer<float>& out, int num);              // mix, room, glue, drive, output

    Telemetry telemetry;

private:
    struct Voice
    {
        bool active = false;
        const Sample* s = nullptr;
        int art = -1, group = 0, chokeGroup = 0;
        uint32_t age = 0;
        double pos = 0.0, inc = 1.0;
        float gain = 1.0f;
        float bend = 0.0f, bendCoef = 0.0f;         // semitones, decaying
        float lp = 1.0f, z0 = 0.0f, z1 = 0.0f;      // one-pole low-pass coefficient and state
        float trans = 0.0f, transEnv = 0.0f, transCoef = 0.0f;
        float fadeIn = 1.0f, fadeInStep = 0.0f;
        int delay = 0, hold = 0;
        float decayGain = 1.0f, decayCoef = 1.0f;
        float fade = 1.0f, fadeStep = 0.0f;         // release / choke
        float room = 0.5f;
    };

    void trigger (int art, const Sample* s, float vel, float gainLin, int offset, double startSec = 0.0, float fadeInMs = 0.0f);
    Voice* allocate();
    void release (Voice& v, float ms);
    float rand01() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (float) (rng & 0xffffff) / 16777216.0f; }
    float randBi() { return (rand01() + rand01() + rand01() - 1.5f) / 1.5f; }   // soft bell curve in -1..1
    const Sample* find (const juce::String& name) const;

    std::vector<std::unique_ptr<Sample>> samples;
    std::vector<Articulation> arts;
    std::array<int, 128> noteMap {};
    const Sample *subKick = nullptr, *kick2 = nullptr, *wireSource = nullptr;

    double sr = 48000.0;
    int latency = 0;
    Settings set;
    std::array<Voice, kMaxVoices> voices;
    uint32_t counter = 0, rng = 0x2545F491u;
    std::array<int, 64> lastLayer {};

    juce::AudioBuffer<float> bus;           // 2 channels per group
    juce::AudioBuffer<float> roomIn;        // stereo send

    // bus processing state
    std::array<float, NumGroups * 2> toneZ {};
    std::array<juce::SmoothedValue<float>, NumGroups> levelSm;
    juce::SmoothedValue<float> roomSm, outSm, driveSm, widthSm;
    float glueEnv = 0.0f, smashEnv = 0.0f;
    std::array<float, NumGroups> fastEnv {}, slowEnv {};

    // small stereo room: 8-line feedback delay network
    static constexpr int kLines = 8;
    std::array<std::vector<float>, kLines> lines;
    std::array<int, kLines> lineW {};
    std::array<float, kLines> damp {};
    std::vector<float> preDelay; int preW = 0;
};
} // namespace rk
