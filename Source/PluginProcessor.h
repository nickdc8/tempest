#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

#include "TempestDSP.h"

class TempestProcessor final : public juce::AudioProcessor
{
public:
    TempestProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Tempest"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }   // the LDR lets go slowly

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Metering for the UI.
    float getGainReductionDb() const { return engine.getGainReductionDb(); }
    float getDrive() const { return engine.getDrive(); }
    bool isOn() const { return on->load() > 0.5f; }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    tempest::Params readParams() const;

    static constexpr int kOversamplingStages = 2;   // 4x

    tempest::Engine engine;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;

    std::atomic<float>* gain = nullptr;
    std::atomic<float>* limiter = nullptr;
    std::atomic<float>* boost = nullptr;
    std::atomic<float>* on = nullptr;
    std::atomic<float>* supply = nullptr;
    std::atomic<float>* isolator = nullptr;
    std::atomic<float>* pickup = nullptr;
    std::atomic<float>* inputLevel = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TempestProcessor)
};
