#include "Kit.h"

namespace rk
{
namespace
{
    // how much PUNCH each group gets: kick, snare, hats, toms, cymbals, perc
    constexpr float kPunchWeight[NumGroups] { 1.0f, 1.0f, 0.25f, 0.85f, 0.0f, 0.5f };
    struct FillDef { const char* name; int layers; int velHi[40]; };
    const FillDef kFills[] {
#include "KitMap.inc"
    };
    inline float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }
}

const Sample* Kit::find (const juce::String& name) const
{
    for (const auto& s : samples) if (s->name == name) return s.get();
    return nullptr;
}

bool Kit::load (const juce::File& root, juce::String& error)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    samples.clear();
    // The extra articulations can't ship inside the plug-in; if they aren't in the bundle,
    // look for the ones the user extracted from their own sound library.
    juce::Array<juce::File> dirs { root.getChildFile ("Rob"), root.getChildFile ("Fill") };
    if (root.getChildFile ("Fill").findChildFiles (juce::File::findFiles, false, "*.wav").isEmpty())
        dirs.add (juce::File ("~/Music/ROB KIT/Samples/Fill"));
    for (const auto& dir : dirs)
        for (const auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.wav"))
            if (std::unique_ptr<juce::AudioFormatReader> r { fm.createReaderFor (f) })
            {
                auto s = std::make_unique<Sample>();
                s->name = f.getFileNameWithoutExtension();
                s->rate = r->sampleRate;
                s->buf.setSize (2, (int) r->lengthInSamples);
                r->read (&s->buf, 0, (int) r->lengthInSamples, 0, true, r->numChannels > 1);
                samples.push_back (std::move (s));
            }

    auto need = [&] (const char* n) -> const Sample*
    {
        auto* s = find (n);
        if (s == nullptr) error << "missing sample " << n << "\n";
        return s;
    };

    arts.clear();
    noteMap.fill (-1);
    auto add = [&] (Articulation a, std::initializer_list<int> notes)
    {
        for (int n : notes) noteMap[(size_t) n] = (int) arts.size();
        arts.push_back (std::move (a));
    };
    auto single = [&] (const char* name, Group g, const char* s, float bend, float bright, float trans, float room)
    {
        Articulation a; a.name = name; a.group = g; a.samples = { need (s) };
        a.bend = bend; a.brightOct = bright; a.transient = trans; a.room = room;
        return a;
    };
    auto layered = [&] (const char* name, Group g, const char* stem, float room)
    {
        Articulation a; a.name = name; a.group = g; a.pick = Pick::Layered; a.room = room;
        a.brightOct = 0.4f; a.transient = 0.1f;
        for (const auto& fd : kFills)
            if (juce::String (fd.name) == stem)
                for (int k = 0; k < fd.layers; ++k)
                {
                    a.samples.push_back (need ((juce::String (stem) + "_v" + juce::String (k + 1).paddedLeft ('0', 2)).toRawUTF8()));
                    a.velHi.push_back (fd.velHi[k]);
                }
        if (a.samples.empty()) error << "no layers for " << stem << "\n";
        return a;
    };

    subKick = need ("SubKick"); kick2 = need ("Kick2"); wireSource = need ("Snare");

    // ---- kick (36 main, 35 second kick); Sub Kick and Kick Blend are mixed in at trigger time
    add (single ("KICK", Kick, "Kick1", 0.7f, 1.2f, 0.5f, 0.25f), { 36 });
    add (single ("KICK 2", Kick, "Kick2", 0.7f, 1.2f, 0.5f, 0.25f), { 35 });
    // ---- snare: soft hits lean off-centre, hard hits centre
    { auto a = single ("SNARE", Snare, "SnareCenter", 0.35f, 2.6f, 0.7f, 1.0f);
      a.pick = Pick::ByVelocity; a.samples = { need ("Snare"), need ("SnareCenter") }; add (a, { 38 }); }
    add (single ("SNARE EDGE", Snare, "Snare", 0.3f, 2.4f, 0.6f, 1.0f), { 34 });
    add (layered ("SIDESTICK", Snare, "SideStick", 0.7f), { 37 });
    add (layered ("RIMSHOT", Snare, "RimShot", 1.0f), { 40 });
    add (layered ("RIM EDGE", Snare, "RimShotEdge", 1.0f), { 32 });
    add (single ("CLAP", Perc, "Clap", 0.0f, 1.5f, 0.3f, 1.0f), { 39 });
    // ---- hats
    { auto a = single ("HAT CLOSED", Hats, "HiHatClosed", 0.0f, 2.0f, 0.6f, 0.45f);
      a.pick = Pick::RoundRobin; a.samples = { need ("HiHatClosed"), need ("HiHatClosed2") };
      a.chokes = 1; a.dampsSelf = false; a.maxVoices = 6; a.detuneCents = 10.0f; add (a, { 42 }); }
    { auto a = layered ("HAT PEDAL", Hats, "HatFootClose", 0.4f); a.chokes = 1; a.dampsSelf = false; add (a, { 44, 33 }); }
    { auto a = single ("HAT OPEN", Hats, "HiHatOpen", 0.0f, 1.8f, 0.5f, 0.5f);
      a.pick = Pick::ByVelocity; a.samples = { need ("HiHatOpenShort"), need ("HiHatOpen") };
      a.chokeGroup = 1; a.dampsSelf = false; a.maxVoices = 4; a.detuneCents = 10.0f; add (a, { 46 }); }
    { auto a = layered ("HAT SPLASH", Hats, "HatFootSplash", 0.5f); a.chokeGroup = 1; a.dampsSelf = false; add (a, { 31 }); }
    // ---- toms
    { auto a = single ("HIGH TOM", Toms, "HiTom", 1.0f, 2.2f, 0.6f, 0.9f); a.detuneCents = 10.0f; add (a, { 48, 50 }); }
    { auto a = single ("MID TOM", Toms, "MidTom", 1.0f, 2.2f, 0.6f, 0.9f); a.detuneCents = 10.0f; add (a, { 45, 47 }); }
    { auto a = single ("LOW TOM", Toms, "LowTom", 1.1f, 2.0f, 0.6f, 0.9f); a.detuneCents = 10.0f; add (a, { 41, 43 }); }
    // ---- cymbals ring over each other; stops choke them
    auto cym = [&] (Articulation a, int chokeGroup) { a.dampsSelf = false; a.maxVoices = 5; a.detuneCents = 12.0f; a.chokeGroup = chokeGroup; return a; };
    add (cym (single ("CRASH L", Cymbals, "CrashL", 0.0f, 2.2f, 0.4f, 0.6f), 2), { 49 });
    add (cym (single ("CRASH R", Cymbals, "CrashR", 0.0f, 2.2f, 0.4f, 0.6f), 4), { 57 });
    add (cym (single ("SPLASH", Cymbals, "Splash", 0.0f, 2.2f, 0.4f, 0.6f), 0), { 55 });
    add (cym (single ("RIDE", Cymbals, "Ride1", 0.0f, 1.6f, 0.5f, 0.45f), 0), { 51 });
    add (cym (single ("RIDE 2", Cymbals, "Ride2", 0.0f, 1.6f, 0.5f, 0.45f), 0), { 59 });
    add (cym (layered ("RIDE BELL", Cymbals, "RideBell", 0.45f), 0), { 53 });
    add (cym (layered ("RIDE EDGE", Cymbals, "RideEdge", 0.45f), 0), { 52 });
    { auto a = cym (layered ("CRASH L STOP", Cymbals, "CrashLStop", 0.5f), 0); a.chokes = 2; add (a, { 28 }); }
    { auto a = cym (layered ("CRASH R STOP", Cymbals, "CrashRStop", 0.5f), 0); a.chokes = 4; add (a, { 29 }); }

    // ---- hand percussion (Logic Studio/Latin Percussion). No guiro, whistle, cuica or vibraslap exist in
    // Logic's library, so those General MIDI notes get the nearest sound it has.
    auto perc = [&] (const char* name, const char* stem, std::initializer_list<int> notes, int chokeGroup = 0, int chokes = 0)
    {
        auto a = layered (name, Perc, stem, 0.6f);
        a.dampsSelf = false; a.maxVoices = 6; a.detuneCents = 6.0f; a.chokeGroup = chokeGroup; a.chokes = chokes;
        add (a, notes);
    };
    perc ("TAMBOURINE", "Tambourine", { 54 });
    perc ("COWBELL", "Cowbell", { 56 });
    perc ("SNAPS", "Snaps", { 58 });
    perc ("BONGO HI", "BongoHi", { 60 });
    perc ("BONGO LO", "BongoLo", { 61 });
    perc ("CONGA MUTE", "CongaMute", { 62 });
    perc ("CONGA OPEN", "CongaOpen", { 63 });
    perc ("CONGA LOW", "CongaLow", { 64 });
    perc ("TIMBALE HI", "TimbaleHi", { 65 });
    perc ("TIMBALE LO", "TimbaleLo", { 66 });
    perc ("AGOGO HI", "AgogoHi", { 67 });
    perc ("AGOGO LO", "AgogoLo", { 68 });
    perc ("CABASA", "Cabasa", { 69, 73 });
    perc ("MARACAS", "Maracas", { 70 });
    perc ("SHEKERE", "Shekere", { 71 });
    perc ("CHIMES", "Chimes", { 72 });
    perc ("CASTANETS", "Castanets", { 74, 85 });
    perc ("CLAVES", "Claves", { 75 });
    perc ("WOOD BLOCK HI", "WoodBlockHi", { 76 });
    perc ("WOOD BLOCK LO", "WoodBlockLo", { 77 });
    perc ("CAJON BASS", "CajonBass", { 78 });
    perc ("CAJON SLAP", "CajonSlap", { 79 });
    perc ("TRIANGLE MUTE", "TriangleMute", { 80 }, 0, 8);
    perc ("TRIANGLE OPEN", "TriangleOpen", { 81 }, 8, 0);
    perc ("SHAKER", "Shaker", { 82 });
    perc ("SLEIGH BELLS", "SleighBells", { 83 });
    perc ("JAM BLOCK HI", "JamBlockHi", { 84 });
    perc ("JAM BLOCK LO", "JamBlockLo", { 86 });
    perc ("STOMP", "Stomp", { 87 });

    for (auto& a : arts)
        for (auto* s : a.samples) if (s == nullptr) { error << "articulation " << a.name << " incomplete\n"; break; }
    return error.isEmpty();
}

void Kit::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    latency = juce::roundToInt (0.003 * sr);           // fixed 3 ms: room for timing drift either side
    bus.setSize (NumGroups * 2, maxBlock);
    roomIn.setSize (2, maxBlock);
    for (auto& v : voices) v.active = false;
    toneZ.fill (0.0f);
    for (auto& s : levelSm) s.reset (sr, 0.03);
    for (auto* s : { &roomSm, &outSm, &driveSm, &widthSm }) s->reset (sr, 0.03);
    for (int g = 0; g < NumGroups; ++g) levelSm[(size_t) g].setCurrentAndTargetValue (dbToGain (set.levelDb[(size_t) g]));
    roomSm.setCurrentAndTargetValue (set.room); outSm.setCurrentAndTargetValue (dbToGain (set.outputDb));
    driveSm.setCurrentAndTargetValue (set.drive); widthSm.setCurrentAndTargetValue (set.width);
    glueEnv = 0.0f; smashEnv = 0.0f; fastEnv.fill (0.0f); slowEnv.fill (0.0f);

    const int base[kLines] { 1031, 1153, 1279, 1361, 1481, 1567, 1693, 1801 };     // samples at 48 kHz, prime-ish
    for (int k = 0; k < kLines; ++k)
    {
        lines[(size_t) k].assign ((size_t) (base[k] * 2.6 * sr / 48000.0) + 8, 0.0f);
        lineW[(size_t) k] = 0; damp[(size_t) k] = 0.0f;
    }
    preDelay.assign ((size_t) (0.03 * sr) + 4, 0.0f); preW = 0;
}

Kit::Voice* Kit::allocate()
{
    Voice* best = nullptr;
    for (auto& v : voices) if (! v.active) return &v;
    for (auto& v : voices) if (v.fadeStep > 0.0f && (! best || v.age < best->age)) best = &v;
    if (! best) for (auto& v : voices) if (! best || v.age < best->age) best = &v;
    return best;   // stolen: the caller overwrites it; at 72 voices this is a rare, quiet tail
}

void Kit::release (Voice& v, float ms)
{
    const float step = 1.0f / std::max (1.0f, ms * 0.001f * (float) sr);
    if (v.fadeStep <= 0.0f || step > v.fadeStep) v.fadeStep = step;
}

void Kit::trigger (int artIndex, const Sample* s, float vel, float gainLin, int offset, double startSec, float fadeInMs)
{
    if (s == nullptr || gainLin < 1.0e-5f) return;
    const auto& a = arts[(size_t) artIndex];
    const float h = set.human, d = set.dynamics;
    auto* v = allocate();
    *v = Voice {};
    v->active = true; v->s = s; v->art = artIndex; v->group = a.group; v->chokeGroup = a.chokeGroup;
    v->age = ++counter; v->room = a.room;

    const float soft = 1.0f - vel;
    const bool layered = a.pick == Pick::Layered;
    // dynamics: a single sample gets level, tone and attack from velocity; real layers mostly carry their own
    float gainDb = -(6.0f + 30.0f * d) * std::pow (soft, 1.4f) * (layered ? 0.8f : 1.0f);
    gainDb += h * 1.2f * randBi();
    v->gain = gainLin * dbToGain (gainDb);

    const float octDark = a.brightOct * std::pow (soft, 1.2f) * (0.4f + d) - h * 0.45f * randBi();
    const float cutoff = juce::jlimit (900.0f, 20000.0f, 20000.0f * std::exp2 (-octDark));
    v->lp = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * std::min (cutoff, 0.45f * (float) sr) / (float) sr);
    if (cutoff >= 19999.0f) v->lp = 1.0f;

    v->trans = juce::jlimit (0.0f, 0.85f, a.transient * std::pow (soft, 1.2f) * (0.3f + d));
    v->transEnv = 1.0f;
    v->transCoef = std::exp (-1.0f / (0.0025f * (float) sr));

    v->bend = a.bend * vel * vel;                                           // a hard hit bends the head sharp
    v->bendCoef = std::exp (-1.0f / (0.045f * (float) sr));

    const float cents = a.detuneCents * h * randBi();
    v->inc = s->rate / sr * std::exp2 ((set.tune[(size_t) a.group] + cents / 100.0f) / 12.0f);
    v->pos = startSec * s->rate;

    const float dec = set.decay[(size_t) a.group];
    if (dec < 0.999f)
    {
        v->hold = (int) (0.025 * sr);
        v->decayCoef = std::exp (-1.0f / ((0.02f + 2.5f * dec * dec * dec) * (float) sr));
    }
    if (fadeInMs > 0.0f) { v->fadeIn = 0.0f; v->fadeInStep = 1.0f / (fadeInMs * 0.001f * (float) sr); }

    const int drift = juce::roundToInt (set.timingMs * 0.001f * (float) sr * randBi());
    v->delay = juce::jlimit (0, 2 * latency, latency + drift) + offset;
}

void Kit::noteOn (int note, float velocity, int offset)
{
    const int ai = articulationForNote (note);
    if (ai < 0) return;
    const auto& a = arts[(size_t) ai];
    const float h = set.human;
    // velocity itself breathes a little: nobody hits the same twice
    const float vel = juce::jlimit (0.02f, 1.0f, velocity * (1.0f + 0.06f * h * randBi()));

    telemetry.hits[(size_t) ai].fetch_add (1, std::memory_order_relaxed);
    telemetry.lastVel[(size_t) ai].store (vel, std::memory_order_relaxed);

    // chokes and self-damping
    for (auto& v : voices)
    {
        if (! v.active) continue;
        if (a.chokes != 0 && (v.chokeGroup & a.chokes) != 0)
            release (v, a.group == Hats ? set.hatCloseMs : 70.0f);
        else if (v.art == ai && a.dampsSelf && v.delay <= 0)
            release (v, 90.0f);
    }
    int count = 0; Voice* oldest = nullptr;
    for (auto& v : voices)
        if (v.active && v.art == ai && v.fadeStep <= 0.0f) { ++count; if (! oldest || v.age < oldest->age) oldest = &v; }
    if (count >= a.maxVoices && oldest) release (*oldest, 20.0f);

    const Sample* s = a.samples.front();
    switch (a.pick)
    {
        case Pick::Single: break;
        case Pick::RoundRobin:
        {
            int pick = rand01() < 0.5f ? 0 : 1;
            if (pick == lastLayer[(size_t) ai] && rand01() < 0.7f) pick ^= 1;      // mostly alternate, never strictly
            lastLayer[(size_t) ai] = pick; s = a.samples[(size_t) pick];
            break;
        }
        case Pick::ByVelocity: break;     // handled below as a continuous blend
        case Pick::Layered:
        {
            // files sharing an upper velocity are round robins of one layer
            const int v127 = juce::roundToInt (vel * 127.0f);
            const int n = (int) a.samples.size();
            auto groupStart = [&] (int i) { while (i > 0 && a.velHi[(size_t) (i - 1)] == a.velHi[(size_t) i]) --i; return i; };
            auto groupEnd   = [&] (int i) { while (i < n - 1 && a.velHi[(size_t) (i + 1)] == a.velHi[(size_t) i]) ++i; return i; };
            int k = 0;
            while (k < n - 1 && v127 > a.velHi[(size_t) k]) ++k;
            k = groupStart (k);
            // a player's hits fall either side of a dynamic: sometimes borrow the neighbouring layer
            if (h > 0.0f && rand01() < 0.45f * h)
            {
                if (rand01() < 0.5f && k > 0) k = groupStart (k - 1);
                else if (groupEnd (k) < n - 1) k = groupEnd (k) + 1;
            }
            const int lo = k, hi = groupEnd (k);
            int pick = lo + (int) (rand01() * (float) (hi - lo + 1));
            pick = juce::jlimit (lo, hi, pick);
            if (pick == lastLayer[(size_t) ai] && hi > lo) pick = pick == hi ? lo : pick + 1;
            lastLayer[(size_t) ai] = pick; s = a.samples[(size_t) pick];
            break;
        }
    }

    if (a.pick == Pick::ByVelocity)
    {
        // Every hit lands somewhere different on the head: blend the soft/off-centre and hard/centre
        // samples by velocity, and let HUMAN move the strike point a little each time.
        float w = juce::jlimit (0.0f, 0.85f, (vel - 0.3f) / 0.5f * 0.85f);
        w = juce::jlimit (0.0f, 1.0f, w + 0.6f * h * randBi());
        // the two strikes sit a fraction of a millisecond apart, a different amount every hit,
        // so the overtones combine differently: like a stick landing a few millimetres away
        const int skew = juce::roundToInt (rand01() * h * 0.0004f * (float) sr);
        trigger (ai, a.samples[0], vel, std::cos (w * 1.5708f), offset + (rand01() < 0.5f ? skew : 0));
        trigger (ai, a.samples[1], vel, std::sin (w * 1.5708f), offset);
    }
    else if (a.group == Kick && note == 36)
    {
        // the beater never lands identically: a little of the second kick, more with HUMAN
        const float b = juce::jlimit (0.0f, 1.0f, set.kickBlend + 0.25f * h * rand01());
        trigger (ai, s, vel, std::cos (b * 1.5708f), offset);
        if (b > 0.001f) trigger (ai, kick2, vel, std::sin (b * 1.5708f), offset + juce::roundToInt (rand01() * h * 0.0003f * (float) sr));
    }
    else trigger (ai, s, vel, 1.0f, offset);

    if (a.group == Kick)
        trigger (ai, subKick, vel, set.sub * 1.4f, offset);

    // snare wires answer the kick and toms
    if ((a.group == Kick || a.group == Toms) && set.wires > 0.001f)
    {
        const int snareArt = articulationForNote (38);
        trigger (snareArt, wireSource, vel, set.wires * 0.16f * vel, offset + juce::roundToInt (0.002 * sr), 0.07, 4.0f);
    }
}

void Kit::allSoundOff() { for (auto& v : voices) if (v.active) release (v, 10.0f); }

void Kit::render (juce::AudioBuffer<float>& /*out*/, int start, int num)
{
    if (start == 0) { bus.clear(); roomIn.clear(); }
    int active = 0;
    for (auto& v : voices)
    {
        if (! v.active) continue;
        ++active;
        const auto& b = v.s->buf;
        const int len = b.getNumSamples();
        const float* L = b.getReadPointer (0);
        const float* R = b.getReadPointer (1);
        float* oL = bus.getWritePointer (v.group * 2) + start;
        float* oR = bus.getWritePointer (v.group * 2 + 1) + start;
        float* rL = roomIn.getWritePointer (0) + start;
        float* rR = roomIn.getWritePointer (1) + start;
        for (int n = 0; n < num; ++n)
        {
            if (v.delay > 0) { --v.delay; continue; }
            const int i = (int) v.pos;
            if (i + 1 >= len) { v.active = false; break; }
            const float fr = (float) (v.pos - (double) i);
            float l = L[i] + (L[i + 1] - L[i]) * fr;
            float r = R[i] + (R[i + 1] - R[i]) * fr;
            if (v.lp < 1.0f) { v.z0 += v.lp * (l - v.z0); v.z1 += v.lp * (r - v.z1); l = v.z0; r = v.z1; }
            float g = v.gain * (1.0f - v.trans * v.transEnv);
            v.transEnv *= v.transCoef;
            if (v.fadeIn < 1.0f) { g *= v.fadeIn; v.fadeIn = std::min (1.0f, v.fadeIn + v.fadeInStep); }
            if (v.hold > 0) --v.hold; else if (v.decayCoef < 1.0f) v.decayGain *= v.decayCoef;
            g *= v.decayGain;
            if (v.fadeStep > 0.0f)
            {
                g *= v.fade;
                v.fade -= v.fadeStep;
                if (v.fade <= 0.0f) { v.active = false; break; }
            }
            oL[n] += l * g; oR[n] += r * g;
            rL[n] += l * g * v.room; rR[n] += r * g * v.room;
            v.pos += v.inc * (v.bend > 0.0005f ? std::exp2 (v.bend / 12.0f) : 1.0);
            v.bend *= v.bendCoef;
            if (v.decayGain < 1.0e-4f) { v.active = false; break; }
        }
    }
    telemetry.voices.store (active, std::memory_order_relaxed);
}

void Kit::finish (juce::AudioBuffer<float>& out, int num)
{
    for (int g = 0; g < NumGroups; ++g) levelSm[(size_t) g].setTargetValue (set.levelDb[(size_t) g] <= -47.9f ? 0.0f : dbToGain (set.levelDb[(size_t) g]));
    roomSm.setTargetValue (set.room); outSm.setTargetValue (dbToGain (set.outputDb));
    driveSm.setTargetValue (set.drive); widthSm.setTargetValue (set.width);

    // room network parameters
    const float fb = 0.55f + 0.38f * set.roomSize;
    const float dampCoef = 0.35f + 0.4f * (1.0f - set.roomSize);
    const int useLen = juce::roundToInt ((0.45 + 0.55 * set.roomSize) * 100.0) ;   // percent of each line

    const float fastAtk = 1.0f - std::exp (-1.0f / (0.0004f * (float) sr)), fastRel = 1.0f - std::exp (-1.0f / (0.012f * (float) sr));
    const float slowAtk = 1.0f - std::exp (-1.0f / (0.012f * (float) sr)), slowRel = 1.0f - std::exp (-1.0f / (0.15f * (float) sr));
    const float smashAtk = 1.0f - std::exp (-1.0f / (0.0002f * (float) sr)), smashRel = 1.0f - std::exp (-1.0f / (0.06f * (float) sr));
    const float tiltHz = 900.0f, a = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * tiltHz / (float) sr);
    const float glueThresh = dbToGain (-14.0f - 8.0f * set.glue), glueRatio = 1.0f + 3.0f * set.glue;
    const float atk = std::exp (-1.0f / (0.003f * (float) sr)), rel = std::exp (-1.0f / (0.12f * (float) sr));
    float pk[2] { 0.0f, 0.0f };
    const int nch = out.getNumChannels();

    for (int n = 0; n < num; ++n)
    {
        float L = 0.0f, R = 0.0f;
        for (int g = 0; g < NumGroups; ++g)
        {
            const float lv = levelSm[(size_t) g].getNextValue();
            const float t = set.tone[(size_t) g];
            const float hi = std::exp2 (t * 1.0f), lo = std::exp2 (-t * 0.5f);
            // PUNCH: fast envelope over slow envelope lifts each hit and sits the ring back
            float tg = 1.0f;
            const float pw = set.punch * kPunchWeight[g];
            if (pw > 0.001f)
            {
                const float m = std::max (std::abs (bus.getSample (g * 2, n)), std::abs (bus.getSample (g * 2 + 1, n)));
                float& f = fastEnv[(size_t) g]; float& sl = slowEnv[(size_t) g];
                f  += (m > f  ? fastAtk : fastRel) * (m - f);
                sl += (m > sl ? slowAtk : slowRel) * (m - sl);
                tg = juce::jlimit (0.55f, 3.2f, std::pow ((f + 1.0e-5f) / (sl + 1.0e-5f), 0.9f * pw));
            }
            for (int c = 0; c < 2; ++c)
            {
                const float x = bus.getSample (g * 2 + c, n);
                float& z = toneZ[(size_t) (g * 2 + c)];
                z += a * (x - z);
                const float y = (z * lo + (x - z) * hi) * lv * tg;
                (c == 0 ? L : R) += y;
            }
        }

        // SMASH: a hard-compressed copy of the kit underneath the dry kit
        if (set.smash > 0.001f)
        {
            const float m = std::max (std::abs (L), std::abs (R));
            smashEnv += (m > smashEnv ? smashAtk : smashRel) * (m - smashEnv);
            const float thr = 0.025f;                                     // about -32 dBFS
            const float gr = smashEnv > thr ? std::pow (smashEnv / thr, 1.0f / 12.0f - 1.0f) : 1.0f;
            const float mk = 9.0f;                                         // +19 dB makeup
            const float amt = set.smash * 0.55f;
            L += amt * std::tanh (L * gr * mk);
            R += amt * std::tanh (R * gr * mk);
        }

        // room
        const float rin = 0.5f * (roomIn.getSample (0, n) + roomIn.getSample (1, n));
        preDelay[(size_t) preW] = rin;
        const int pd = (int) (0.012 * sr * (0.5 + set.roomSize));
        const float pre = preDelay[(size_t) ((preW - pd + (int) preDelay.size()) % (int) preDelay.size())];
        preW = (preW + 1) % (int) preDelay.size();
        float o[kLines];
        for (int k = 0; k < kLines; ++k)
        {
            auto& ln = lines[(size_t) k];
            const int L0 = std::max (8, (int) (ln.size() - 8) * useLen / 100);
            const int r = (lineW[(size_t) k] - L0 + (int) ln.size()) % (int) ln.size();
            damp[(size_t) k] += dampCoef * (ln[(size_t) r] - damp[(size_t) k]);
            o[k] = damp[(size_t) k];
        }
        float hsum = 0.0f; for (float v : o) hsum += v;
        hsum *= 2.0f / kLines;                                       // Householder feedback
        for (int k = 0; k < kLines; ++k)
        {
            auto& ln = lines[(size_t) k];
            ln[(size_t) lineW[(size_t) k]] = juce::jlimit (-4.0f, 4.0f, pre + fb * (o[k] - hsum));
            lineW[(size_t) k] = (lineW[(size_t) k] + 1) % (int) ln.size();
        }
        const float rm = roomSm.getNextValue() * 0.9f;
        L += rm * (o[0] - o[2] + o[4] - o[6]) * 0.5f;
        R += rm * (o[1] - o[3] + o[5] - o[7]) * 0.5f;

        // glue: gentle bus compression
        const float lvl = std::max (std::abs (L), std::abs (R));
        glueEnv = lvl > glueEnv ? atk * glueEnv + (1.0f - atk) * lvl : rel * glueEnv + (1.0f - rel) * lvl;
        float gr = 1.0f;
        if (set.glue > 0.001f && glueEnv > glueThresh)
            gr = std::pow (glueEnv / glueThresh, 1.0f / glueRatio - 1.0f);
        const float makeup = 1.0f + 0.5f * set.glue;
        L *= gr * makeup; R *= gr * makeup;

        // drive: tape-ish soft saturation, level-compensated
        const float dr = driveSm.getNextValue();
        if (dr > 0.001f)
        {
            const float k = 1.0f + 6.0f * dr;
            L = (std::tanh (L * k) / k) * (1.0f + 0.6f * dr);
            R = (std::tanh (R * k) / k) * (1.0f + 0.6f * dr);
        }

        // width (mid/side), output, safety
        const float w = widthSm.getNextValue();
        const float M = 0.5f * (L + R), S = 0.5f * (L - R) * w;
        const float og = outSm.getNextValue();
        L = (M + S) * og; R = (M - S) * og;
        for (float* x : { &L, &R })
        {
            if (! std::isfinite (*x)) *x = 0.0f;
            const float ax = std::abs (*x);
            if (ax > 0.9f) *x = std::copysign (0.9f + 0.1f * std::tanh ((ax - 0.9f) / 0.1f), *x);
        }
        pk[0] = std::max (pk[0], std::abs (L)); pk[1] = std::max (pk[1], std::abs (R));
        out.setSample (0, n, nch == 1 ? 0.5f * (L + R) : L);
        if (nch > 1) out.setSample (1, n, R);
    }
    for (int c = 0; c < 2; ++c)
    {
        float prev = telemetry.peak[c].load (std::memory_order_relaxed);
        while (pk[c] > prev && ! telemetry.peak[c].compare_exchange_weak (prev, pk[c], std::memory_order_relaxed)) {}
    }
}
} // namespace rk
