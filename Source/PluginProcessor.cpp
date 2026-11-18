#include "PluginProcessor.h"
#include "PluginEditor.h"

TempestProcessor::TempestProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "state", createLayout())
{
    gain       = apvts.getRawParameterValue ("gain");
    limiter    = apvts.getRawParameterValue ("limiter");
    boost      = apvts.getRawParameterValue ("boost");
    on         = apvts.getRawParameterValue ("on");
    supply     = apvts.getRawParameterValue ("supply");
    isolator   = apvts.getRawParameterValue ("isolator");
    pickup     = apvts.getRawParameterValue ("pickup");
    inputLevel = apvts.getRawParameterValue ("inputLevel");
}

juce::AudioProcessorValueTreeState::ParameterLayout TempestProcessor::createLayout()
{
    using Range = juce::NormalisableRange<float>;
    using Float = juce::AudioParameterFloat;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // The three knobs are knob positions, 0 to 10, as printed on a pedal.
    // Each maps to its pot's taper in the engine.
    auto knob = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<Float> (
            juce::ParameterID { id, 1 }, name, Range (0.0f, 10.0f, 0.01f), def,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1); }));
    };

    layout.add (knob ("gain", "Gain", 6.0f));
    layout.add (knob ("limiter", "Limiter", 4.0f));
    layout.add (knob ("boost", "Boost", 3.5f));

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "on", 1 }, "On", true));

    // The internals: things set once, inside the box.
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "supply", 1 }, "Supply", juce::StringArray { "9 V", "12 V" }, 0));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "isolator", 1 }, "Isolator Cap", false));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "pickup", 1 }, "Pickup Load",
        juce::StringArray { "Off", "Single Coil", "Humbucker" }, 2));

    // How many volts a full-scale sample stands for. A humbucker DI that
    // peaks around -6 dBFS is about right at 0 dBV.
    layout.add (std::make_unique<Float> (
        juce::ParameterID { "inputLevel", 1 }, "Input Level", Range (-12.0f, 12.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dBV")));

    return layout;
}

tempest::Params TempestProcessor::readParams() const
{
    tempest::Params p;
    p.gain       = gain->load() / 10.0;
    p.limiter    = limiter->load() / 10.0;
    p.boost      = boost->load() / 10.0;
    p.on         = on->load() > 0.5f;
    p.supply     = juce::roundToInt (supply->load());
    p.isolator   = isolator->load() > 0.5f;
    p.pickup     = juce::roundToInt (pickup->load());
    p.inputVolts = juce::Decibels::decibelsToGain ((double) inputLevel->load());
    return p;
}

bool TempestProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && layouts.getMainInputChannelSet() == out;
}

void TempestProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // 4x oversampling: the LM386 clips hard with 28 dB in front of it, and
    // the rectified LED drive is full of harmonics. The dry signal goes
    // through the same path, so switching off stays time-aligned.
    const auto channels = (size_t) juce::jmax (1, getTotalNumOutputChannels());
    oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
        channels, kOversamplingStages, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
    oversampling->initProcessing ((size_t) samplesPerBlock);
    setLatencySamples (juce::roundToInt (oversampling->getLatencyInSamples()));

    engine.setParams (readParams());
    engine.prepare (sampleRate * (double) (1 << kOversamplingStages));
}

void TempestProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    engine.setParams (readParams());

    juce::dsp::AudioBlock<float> block (buffer);
    auto up = oversampling->processSamplesUp (block);

    float* channels[tempest::Engine::kMaxChannels] = {};
    const int numChannels = juce::jmin ((int) up.getNumChannels(), tempest::Engine::kMaxChannels);
    for (int ch = 0; ch < numChannels; ++ch)
        channels[ch] = up.getChannelPointer ((size_t) ch);

    engine.process (channels, numChannels, (int) up.getNumSamples());

    oversampling->processSamplesDown (block);
}

juce::AudioProcessorEditor* TempestProcessor::createEditor()
{
    return new TempestEditor (*this);
}

void TempestProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void TempestProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TempestProcessor();
}
