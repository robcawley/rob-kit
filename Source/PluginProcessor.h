#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "DSP/Kit.h"
#include "MidiLearn.h"
#include <string_view>

class RobKitProcessor : public juce::AudioProcessor
{
public:
    RobKitProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "ROB KIT"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 12.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "ROB KIT"; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    // shared with the UI controls library
    const std::vector<juce::RangedAudioParameter*>& parameterList() const { return plist; }
    int parameterIndex (const juce::String& id) const;
    float effectiveValue (int index) const { return raw[(size_t) index]->load (std::memory_order_relaxed); }
    MidiLearn& midiLearn() { return learn; }
    juce::UndoManager& undoManager() { return undo; }

    const rk::Kit& kit() const { return engine; }
    rk::Telemetry& telemetry() { return engine.telemetry; }
    juce::String loadError() const { return loadErr; }
    void audition (int note, float velocity);           // message thread: play a pad

private:
    rk::Settings readSettings() const;
    float get (const juce::String& id) const { return raw[(size_t) parameterIndex (id)]->load (std::memory_order_relaxed); }

    std::vector<juce::RangedAudioParameter*> plist;
    std::vector<std::atomic<float>*> raw;
    std::map<juce::String, int> idIndex;
    std::array<int, NumGroups * 4> groupIdx {};
    std::array<int, 15> miscIdx {};

    rk::Kit engine;
    juce::String loadErr;
    MidiLearn learn;
    juce::UndoManager undo { 30000, 30 };

    static constexpr int kQueue = 256;
    juce::AbstractFifo auditionFifo { kQueue };
    std::array<int, kQueue> auditionNote {};
    std::array<float, kQueue> auditionVel {};
};
