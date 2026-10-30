#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // Samples live inside the plug-in bundle (Contents/Resources/Samples); the source tree is a fallback.
    juce::File findSamples()
    {
        const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
        for (auto f : { exe.getParentDirectory().getParentDirectory().getChildFile ("Resources/Samples"),
                        exe.getParentDirectory().getChildFile ("Samples"),
                        juce::File ("~/Music/ROB KIT/Samples") })
            if (f.getChildFile ("Rob").isDirectory()) return f;
        return {};
    }
}

RobKitProcessor::RobKitProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "ROB_KIT", createParameterLayout())
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            idIndex[rp->getParameterID()] = (int) plist.size();
            plist.push_back (rp);
            raw.push_back (apvts.getRawParameterValue (rp->getParameterID()));
        }
    for (int g = 0; g < NumGroups; ++g)
    {
        groupIdx[(size_t) (g * 4 + 0)] = parameterIndex (PID::level (g));
        groupIdx[(size_t) (g * 4 + 1)] = parameterIndex (PID::tune (g));
        groupIdx[(size_t) (g * 4 + 2)] = parameterIndex (PID::decay (g));
        groupIdx[(size_t) (g * 4 + 3)] = parameterIndex (PID::tone (g));
    }
    const char* misc[] { PID::dynamics, PID::human, PID::timing, PID::kickBlend, PID::sub, PID::wires, PID::hatClose,
                         PID::room, PID::roomSize, PID::glue, PID::drive, PID::width, PID::output, PID::punch, PID::smash };
    for (size_t k = 0; k < 15; ++k) miscIdx[k] = parameterIndex (misc[k]);

    const auto dir = findSamples();
    if (! dir.isDirectory()) loadErr = "Sample folder not found";
    else engine.load (dir, loadErr);
    learn.attach (plist, raw);
}

int RobKitProcessor::parameterIndex (const juce::String& id) const
{
    const auto it = idIndex.find (id);
    jassert (it != idIndex.end());
    return it == idIndex.end() ? 0 : it->second;
}

bool RobKitProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void RobKitProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.setSettings (readSettings());
    engine.prepare (sampleRate, std::max (samplesPerBlock, 32));
    setLatencySamples (engine.latencySamples());
}

rk::Settings RobKitProcessor::readSettings() const
{
    rk::Settings s;
    auto v = [this] (int i) { return raw[(size_t) i]->load (std::memory_order_relaxed); };
    for (int g = 0; g < NumGroups; ++g)
    {
        s.levelDb[(size_t) g] = v (groupIdx[(size_t) (g * 4)]);
        s.tune[(size_t) g]    = v (groupIdx[(size_t) (g * 4 + 1)]);
        s.decay[(size_t) g]   = v (groupIdx[(size_t) (g * 4 + 2)]);
        s.tone[(size_t) g]    = v (groupIdx[(size_t) (g * 4 + 3)]);
    }
    s.dynamics = v (miscIdx[0]); s.human = v (miscIdx[1]); s.timingMs = v (miscIdx[2]); s.kickBlend = v (miscIdx[3]);
    s.sub = v (miscIdx[4]); s.wires = v (miscIdx[5]); s.hatCloseMs = v (miscIdx[6]);
    s.room = v (miscIdx[7]); s.roomSize = v (miscIdx[8]); s.glue = v (miscIdx[9]); s.drive = v (miscIdx[10]);
    s.width = v (miscIdx[11]); s.outputDb = v (miscIdx[12]); s.punch = v (miscIdx[13]); s.smash = v (miscIdx[14]);
    return s;
}

void RobKitProcessor::audition (int note, float velocity)
{
    int s1, n1, s2, n2;
    auditionFifo.prepareToWrite (1, s1, n1, s2, n2);
    if (n1 + n2 == 0) return;
    const int k = n1 > 0 ? s1 : s2;
    auditionNote[(size_t) k] = note; auditionVel[(size_t) k] = velocity;
    auditionFifo.finishedWrite (1);
}

void RobKitProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int num = buffer.getNumSamples();
    buffer.clear();
    engine.setSettings (readSettings());

    {   // pads clicked in the window
        int s1, n1, s2, n2;
        auditionFifo.prepareToRead (auditionFifo.getNumReady(), s1, n1, s2, n2);
        for (int k = s1; k < s1 + n1; ++k) engine.noteOn (auditionNote[(size_t) k], auditionVel[(size_t) k], 0);
        for (int k = s2; k < s2 + n2; ++k) engine.noteOn (auditionNote[(size_t) k], auditionVel[(size_t) k], 0);
        auditionFifo.finishedRead (n1 + n2);
    }

    int cursor = 0;
    for (const auto meta : midi)
    {
        if (meta.numBytes > 3) continue;                  // never copy long SysEx on the audio thread
        const int pos = juce::jlimit (0, num, meta.samplePosition);
        if (pos > cursor) { engine.render (buffer, cursor, pos - cursor); cursor = pos; }
        const auto m = meta.getMessage();
        if (m.isNoteOn()) engine.noteOn (m.getNoteNumber(), m.getFloatVelocity(), 0);
        else if (m.isAllSoundOff() || m.isAllNotesOff()) engine.allSoundOff();
        else if (m.isController()) learn.handleController (m.getControllerNumber(), m.getControllerValue());
    }
    if (cursor < num) engine.render (buffer, cursor, num - cursor);
    engine.finish (buffer, num);
}

void RobKitProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    for (size_t k = 0; k < plist.size(); ++k)
        state.getChildWithProperty ("id", plist[k]->getParameterID()).setProperty ("value", raw[k]->load (std::memory_order_relaxed), nullptr);
    learn.save (state);
    state.setProperty ("stateVersion", 1, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void RobKitProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            learn.load (tree);
            apvts.replaceState (tree);
        }
}

juce::AudioProcessorEditor* RobKitProcessor::createEditor() { return new RobKitEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new RobKitProcessor(); }
