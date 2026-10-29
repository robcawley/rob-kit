#include <juce_audio_formats/juce_audio_formats.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstdlib>
#include <new>

// allocation probe, armed only around processBlock
thread_local bool probe = false;
thread_local uint64_t allocs = 0;
void* operator new (std::size_t n) { if (probe) ++allocs; if (void* p = std::malloc (std::max (n, std::size_t (1)))) return p; throw std::bad_alloc(); }
void* operator new[] (std::size_t n) { return ::operator new (n); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

namespace
{
constexpr double SR = 48000.0;
void set (RobKitProcessor& p, const juce::String& id, float v)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

struct Ev { double beat; int note; int vel; };

// A Session Player-style groove: 16th hats with a human accent shape, kick/snare with ghosts,
// an open hat and pedal, a crash on the one, a tom fill into bar 4. Timing nudged by a few ms.
std::vector<Ev> groove (int bars)
{
    std::vector<Ev> e;
    juce::Random r (7);
    auto jit = [&] (double ms) { return (r.nextDouble() * 2.0 - 1.0) * ms / 1000.0 * (96.0 / 60.0); };
    for (int b = 0; b < bars; ++b)
    {
        const double o = b * 4.0;
        if (b % 4 == 0) e.push_back ({ o + jit (2), 49, 112 });
        for (int s = 0; s < 16; ++s)
        {
            const double t = o + s * 0.25;
            const bool fill = (b % 4 == 3) && s >= 12;
            if (! fill)
            {
                if (s == 14 && b % 2 == 1) e.push_back ({ t + jit (4), 46, 88 });
                else e.push_back ({ t + jit (5), 42, s % 4 == 0 ? 96 + r.nextInt (14) : (s % 2 == 0 ? 74 + r.nextInt (12) : 52 + r.nextInt (16)) });
                if (s == 15 && b % 2 == 1) e.push_back ({ t + 0.12 + jit (3), 44, 70 });
            }
            if (s == 0 || s == 7 || s == 10) e.push_back ({ t + jit (3), 36, s == 0 ? 118 : 100 + r.nextInt (10) });
            if (s == 4 || s == 12) e.push_back ({ t + jit (3), 38, 112 + r.nextInt (10) });
            if (! fill && (s == 6 || s == 9 || s == 15)) e.push_back ({ t + jit (6), 38, 26 + r.nextInt (16) });
            if (fill) e.push_back ({ t + jit (4), s < 14 ? 48 : (s == 14 ? 47 : 43), 90 + r.nextInt (24) });
            if (b >= 4)
            {
                if (s == 4 || s == 12) e.push_back ({ t + jit (6), 54, 90 + r.nextInt (20) });
                e.push_back ({ t + jit (7), 82, s % 2 == 0 ? 80 + r.nextInt (20) : 45 + r.nextInt (20) });
                if (b == 7 && s % 4 == 0) e.push_back ({ t + jit (4), 56, 100 });
            }
        }
    }
    return e;
}

std::vector<float> render (RobKitProcessor& p, const std::vector<Ev>& ev, double seconds, int bs = 256, double bpm = 96.0)
{
    p.setPlayConfigDetails (0, 2, SR, bs); p.prepareToPlay (SR, bs);
    const int total = (int) (seconds * SR);
    std::vector<float> out; out.reserve ((size_t) total * 2);
    juce::AudioBuffer<float> blk (2, bs);
    for (int pos = 0; pos < total; pos += bs)
    {
        juce::MidiBuffer mb;
        for (const auto& x : ev)
        {
            const int s = (int) (x.beat * 60.0 / bpm * SR);
            if (s >= pos && s < pos + bs) mb.addEvent (juce::MidiMessage::noteOn (10, x.note, (juce::uint8) juce::jlimit (1, 127, x.vel)), s - pos);
        }
        p.processBlock (blk, mb);
        for (int n = 0; n < bs; ++n) { out.push_back (blk.getSample (0, n)); out.push_back (blk.getSample (1, n)); }
    }
    return out;
}

void writeWav (const juce::File& f, const std::vector<float>& v)
{
    f.deleteFile();
    juce::AudioBuffer<float> b (2, (int) v.size() / 2);
    for (int n = 0; n < b.getNumSamples(); ++n) { b.setSample (0, n, v[(size_t) n * 2]); b.setSample (1, n, v[(size_t) n * 2 + 1]); }
    juce::WavAudioFormat w;
    if (auto wr = std::unique_ptr<juce::AudioFormatWriter> (w.createWriterFor (new juce::FileOutputStream (f), SR, 2, 24, {}, 0)))
        wr->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

float peakDb (const std::vector<float>& v) { float m = 0; for (float x : v) m = std::max (m, std::abs (x)); return juce::Decibels::gainToDecibels (m, -120.0f); }

int grooveTest (const juce::File& dir)
{
    dir.createDirectory();
    RobKitProcessor p;
    if (p.loadError().isNotEmpty()) { std::printf ("LOAD ERROR: %s\n", p.loadError().toRawUTF8()); return 1; }
    const auto ev = groove (8);
    auto v = render (p, ev, 8 * 4 * 60.0 / 96.0 + 3.0);
    writeWav (dir.getChildFile ("groove-96bpm.wav"), v);
    int bad = 0; for (float x : v) bad += std::isfinite (x) ? 0 : 1;
    double sq = 0; for (float x : v) sq += x * x;
    std::printf ("groove: %zu events, peak %.1f dBFS, rms %.1f dBFS, non-finite %d -> %s\n", ev.size(), peakDb (v),
                 juce::Decibels::gainToDecibels (std::sqrt (sq / (double) v.size()), -120.0), bad, dir.getChildFile ("groove-96bpm.wav").getFullPathName().toRawUTF8());
    return bad == 0 && peakDb (v) <= 0.0f ? 0 : 1;
}

// Repeat test: 16 identical snare hits at the same velocity. With HUMAN up, no two consecutive hits
// should be sample-identical; report how alike they are (1.0 = identical).
int repeatTest()
{
    bool ok = true;
    for (int note : { 38, 42, 36, 37 })
        for (float h : { 0.0f, 0.5f, 1.0f })
        {
            RobKitProcessor p; set (p, PID::human, h); set (p, PID::room, 0.0f); set (p, PID::wires, 0.0f);
            std::vector<Ev> ev; for (int k = 0; k < 16; ++k) ev.push_back ({ k * 1.0, note, 100 });
            const auto v = render (p, ev, 16 * 60.0 / 96.0 + 1.0);
            const int hop = (int) (60.0 / 96.0 * SR), win = (int) (0.08 * SR), lat = p.getLatencySamples();
            double sumC = 0, maxC = 0; int identical = 0;
            for (int k = 1; k < 16; ++k)
            {
                double ab = 0, aa = 0, bb = 0; bool same = true;
                for (int n = 0; n < win; ++n)
                {
                    const float a = v[(size_t) ((lat + (k - 1) * hop + n) * 2)], b = v[(size_t) ((lat + k * hop + n) * 2)];
                    ab += a * b; aa += a * a; bb += b * b; same &= a == b;
                }
                const double c = ab / std::sqrt (aa * bb + 1e-30);
                sumC += c; maxC = std::max (maxC, c); identical += same ? 1 : 0;
            }
            const bool pass = h <= 0.0f || identical == 0;
            ok &= pass;
            std::printf ("note %2d human %3.0f%%: consecutive similarity mean %.4f max %.4f, identical pairs %2d %s\n",
                         note, h * 100, sumC / 15, maxC, identical, h > 0 ? (pass ? "PASS" : "FAIL") : "(reference)");
        }
    return ok ? 0 : 1;
}

// Velocity response: level and brightness must rise steadily with velocity.
int velocityTest()
{
    bool ok = true;
    for (int note : { 38, 36, 42, 48, 51, 37, 53 })
    {
        std::printf ("note %2d:", note);
        double prevRms = -1e9;
        for (int vel : { 10, 30, 50, 70, 90, 110, 127 })
        {
            RobKitProcessor p; set (p, PID::human, 0.0f); set (p, PID::room, 0.0f);
            const auto v = render (p, { { 0.0, note, vel } }, 0.6);
            double sq = 0, hf = 0, prev = 0;
            for (size_t n = 0; n < v.size(); n += 2) { sq += v[n] * v[n]; const double d = v[n] - prev; hf += d * d; prev = v[n]; }
            const double rms = 10 * std::log10 (sq / (v.size() / 2) + 1e-20);
            const double bright = 10 * std::log10 (hf / (sq + 1e-20) + 1e-20);
            std::printf ("  v%3d %5.1f dB/%4.1f", vel, rms, bright);
            ok &= rms > prevRms - 0.5; prevRms = rms;
        }
        std::printf ("\n");
    }
    std::printf ("level rises with velocity on every drum: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

// Impact: how far each hit's attack stands above its body, and overall density, with PUNCH/SMASH off vs default.
int impactTest()
{
    for (int note : { 36, 38, 48 })
    {
        for (int on : { 0, 1 })
        {
            RobKitProcessor p; set (p, PID::human, 0.0f); set (p, PID::room, 0.0f);
            if (! on) { set (p, PID::punch, 0.0f); set (p, PID::smash, 0.0f); }
            const auto v = render (p, { { 0.0, note, 110 } }, 0.5);
            const int lat = p.getLatencySamples();
            double atk = 0, body = 0;
            for (int n = lat; n < lat + (int) (0.012 * SR); ++n) atk = std::max (atk, (double) std::abs (v[(size_t) n * 2]));
            for (int n = lat + (int) (0.06 * SR); n < lat + (int) (0.2 * SR); ++n) body += v[(size_t) n * 2] * v[(size_t) n * 2];
            body = std::sqrt (body / (0.14 * SR));
            std::printf ("note %d %-8s attack %.1f dBFS, body %.1f dBFS, attack over body %.1f dB\n", note, on ? "default" : "off",
                juce::Decibels::gainToDecibels (atk), juce::Decibels::gainToDecibels (body), juce::Decibels::gainToDecibels (atk / body));
        }
    }
    return 0;
}

int mapTest()
{
    RobKitProcessor p;
    const auto& kit = p.kit();
    std::vector<int> need;                       // 28-87, except 30 (unused by Session Player and GM)
    for (int n = 28; n <= 87; ++n) if (n != 30) need.push_back (n);
    int missing = 0;
    for (int n : need)
    {
        const int a = kit.articulationForNote (n);
        std::printf ("%3d %-5s -> %s\n", n, juce::MidiMessage::getMidiNoteName (n, true, true, 3).toRawUTF8(),
                     a >= 0 ? kit.articulations()[(size_t) a].name.toRawUTF8() : "MISSING");
        missing += a < 0;
    }
    std::printf ("Notes 28-87 (kit + General MIDI percussion) covered: %d of %zu %s\n", (int) need.size() - missing, need.size(), missing ? "FAIL" : "PASS");
    std::printf ("load: %s\n", p.loadError().isEmpty() ? "all samples loaded" : p.loadError().toRawUTF8());
    return missing == 0 && p.loadError().isEmpty() ? 0 : 1;
}

int rtTest()
{
    uint64_t total = 0; bool finite = true; float pk = 0;
    for (double sr : { 44100.0, 48000.0, 96000.0 }) for (int bs : { 1, 64, 512 })
    {
        RobKitProcessor p; set (p, PID::timing, 3.0f); set (p, PID::drive, 1.0f); set (p, PID::room, 1.0f);
        p.setPlayConfigDetails (0, 2, sr, bs); p.prepareToPlay (sr, bs);
        juce::AudioBuffer<float> b (2, bs); juce::MidiBuffer m; m.ensureSize (8192);
        juce::Random r (3);
        const int blocks = (int) (sr * 4 / bs);
        for (int k = 0; k < blocks; ++k)
        {
            m.clear();
            if (r.nextInt (std::max (1, 2048 / bs)) == 0)
                for (int j = 0; j < 4; ++j) m.addEvent (juce::MidiMessage::noteOn (10, 28 + r.nextInt (32), (juce::uint8) (1 + r.nextInt (127))), r.nextInt (bs));
            for (int j = 0; j < 32; ++j) if (k % 5 == 0) m.addEvent (juce::MidiMessage::noteOn (10, 42, (juce::uint8) 127), 0);   // voice flood
            probe = true; p.processBlock (b, m); probe = false;
            for (int c = 0; c < 2; ++c) for (int n = 0; n < bs; ++n) { const float x = b.getSample (c, n); finite &= std::isfinite (x); pk = std::max (pk, std::abs (x)); }
        }
        total += allocs; allocs = 0;
        std::printf ("rt %.0f Hz / %d: latency %d samples\n", sr, bs, p.getLatencySamples());
    }
    std::printf ("allocations in processBlock %llu, finite %d, peak %.2f dBFS -> %s\n", (unsigned long long) total, finite, juce::Decibels::gainToDecibels (pk),
                 total == 0 && finite && pk <= 1.0f ? "PASS" : "FAIL");
    return total == 0 && finite && pk <= 1.0f ? 0 : 1;
}

int stateTest()
{
    RobKitProcessor a; juce::Random r (5);
    for (auto* prm : a.parameterList()) prm->setValueNotifyingHost (r.nextFloat());
    a.midiLearn().startLearning (a.parameterIndex (PID::human));
    { juce::AudioBuffer<float> b (2, 64); juce::MidiBuffer m; m.addEvent (juce::MidiMessage::controllerEvent (1, 20, 90), 0);
      a.setPlayConfigDetails (0, 2, SR, 64); a.prepareToPlay (SR, 64); a.processBlock (b, m); }
    juce::MemoryBlock blob; a.getStateInformation (blob);
    RobKitProcessor b; b.setStateInformation (blob.getData(), (int) blob.getSize());
    int diffs = 0;
    for (size_t k = 0; k < a.parameterList().size(); ++k)
        diffs += std::abs (a.effectiveValue ((int) k) - b.effectiveValue ((int) k)) > 1e-4f * (1.0f + std::abs (a.effectiveValue ((int) k)));
    const bool learn = b.midiLearn().ccFor (b.parameterIndex (PID::human)) == 20;
    std::printf ("state: %zu parameters, %d differ after reload; MIDI-learn map restored: %s -> %s\n", a.parameterList().size(), diffs,
                 learn ? "yes" : "no", diffs == 0 && learn ? "PASS" : "FAIL");
    return diffs == 0 && learn ? 0 : 1;
}

int uiTest (const juce::File& dir)
{
    dir.createDirectory();
    RobKitProcessor p;
    p.setPlayConfigDetails (0, 2, SR, 256); p.prepareToPlay (SR, 256);
    std::unique_ptr<RobKitEditor> ed (dynamic_cast<RobKitEditor*> (p.createEditor()));
    auto& panel = ed->getPanel();
    juce::AudioBuffer<float> b (2, 256);
    bool ok = true;

    // 1. every parameter has exactly one control on the panel
    std::map<juce::String, int> bound;
    for (auto* c : panel.boundControls()) ++bound[c->parameter().getParameterID()];
    int missing = 0, dup = 0;
    for (auto* prm : p.parameterList())
    {
        const int n = bound[prm->getParameterID()];
        if (n == 0) { std::printf ("  not on panel: %s\n", prm->getParameterID().toRawUTF8()); ++missing; }
        if (n > 1) ++dup;
    }
    std::printf ("controls bound: %d for %zu parameters, %d missing, %d duplicated -> %s\n", panel.boundControlCount(),
                 p.parameterList().size(), missing, dup, missing == 0 && dup == 0 ? "PASS" : "FAIL");
    ok &= missing == 0 && dup == 0;

    // 2. each kit part: the centre of its hit area selects that part, and clicking it plays its articulation
    auto& stage = panel.stage();
    for (int i = 0; i < (int) stage.parts().size(); ++i)
    {
        const auto& part = stage.parts()[(size_t) i];
        const auto e = stage.hitEllipse (i);
        const int hit = stage.partAt (e.getCentre());
        const int art = p.kit().articulationForNote (part.note);
        const auto before = art >= 0 ? p.telemetry().hits[(size_t) art].load() : 0u;
        const int velTop = juce::roundToInt (stage.velocityAt (i, { e.getCentreX(), e.getY() + 1.0f }));
        const int velLow = juce::roundToInt (stage.velocityAt (i, { e.getCentreX(), e.getBottom() - 1.0f }));
        stage.onHit (i, 100);
        juce::MidiBuffer m; p.processBlock (b, m);
        const bool played = art >= 0 && p.telemetry().hits[(size_t) art].load() == before + 1;
        const bool pass = hit == i && played && velTop > velLow;
        std::printf ("  %-9s centre->%-9s note %2d -> %-12s plays %-3s vel top/bottom %3d/%3d %s\n", part.label.toRawUTF8(),
                     hit >= 0 ? stage.parts()[(size_t) hit].label.toRawUTF8() : "none", part.note,
                     art >= 0 ? p.kit().articulations()[(size_t) art].name.toRawUTF8() : "MISSING", played ? "yes" : "no", velTop, velLow, pass ? "PASS" : "FAIL");
        ok &= pass;
    }

    // 3. host automation moves the panel: change a parameter, the bound control follows
    {
        auto* prm = p.apvts.getParameter (PID::human);
        prm->setValueNotifyingHost (0.9f);
        float shown = -1.0f;
        for (auto* c : panel.boundControls()) if (c->parameter().getParameterID() == PID::human) shown = c->norm();
        const bool pass = std::abs (shown - 0.9f) < 1.0e-4f;
        std::printf ("host automation reaches the control (Human 0.9 -> panel %.3f): %s\n", shown, pass ? "PASS" : "FAIL");
        ok &= pass;
    }

    for (int k = 0; k < 12; ++k)
    {
        juce::MidiBuffer m;
        if (k == 6) for (int n : { 36, 42, 38, 49 }) m.addEvent (juce::MidiMessage::noteOn (10, n, (juce::uint8) (n == 38 ? 120 : 90)), 0);
        p.processBlock (b, m); ed->tickForTest();
    }
    for (float s : { 1.0f, 2.0f })
    {
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, s);
        auto f = dir.getChildFile (s > 1 ? "robkit@2x.png" : "robkit.png"); f.deleteFile();
        juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os);
        std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
    }
    // idle cost: after the glow has faded, nothing should repaint
    {
        for (int k = 0; k < 20; ++k) { juce::MidiBuffer m; p.processBlock (b, m); ed->tickForTest(); }
        const bool idle = ! panel.stage().tick();
        std::printf ("kit stage stops repainting when idle: %s\n", idle ? "PASS" : "FAIL");
        ok &= idle;
    }
    std::printf ("UI %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    std::setvbuf (stdout, nullptr, _IOLBF, 0);
    const juce::String mode (argc > 1 ? argv[1] : "--map");
    const juce::File dir (argc > 2 ? argv[2] : "renders");
    if (mode == "--groove") return grooveTest (dir);
    if (mode == "--repeat") return repeatTest();
    if (mode == "--velocity") return velocityTest();
    if (mode == "--map") return mapTest();
    if (mode == "--impact") return impactTest();
    if (mode == "--rt") return rtTest();
    if (mode == "--state") return stateTest();
    if (mode == "--ui") return uiTest (dir);
    std::fprintf (stderr, "modes: --map --groove dir --repeat --velocity --rt --state --ui dir\n");
    return 2;
}
