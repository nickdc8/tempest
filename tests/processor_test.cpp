// Drives TempestProcessor the way a host would, without a host.

#include "../Source/PluginProcessor.h"

static int failures = 0;

static void check (bool ok, const juce::String& what)
{
    std::cout << "  [" << (ok ? "ok" : "FAIL") << "] " << what << std::endl;
    failures += ok ? 0 : 1;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;

    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (int channels : { 1, 2 })
            for (int blockSize : { 32, 512, 1024 })
            {
                TempestProcessor p;
                const auto set = channels == 1 ? juce::AudioChannelSet::mono()
                                               : juce::AudioChannelSet::stereo();
                juce::AudioProcessor::BusesLayout layout;
                layout.inputBuses.add (set);
                layout.outputBuses.add (set);
                const bool layoutOk = p.setBusesLayout (layout);

                p.setRateAndBufferSizeDetails (rate, blockSize);
                p.prepareToPlay (rate, blockSize);

                juce::AudioBuffer<float> buffer (channels, blockSize);
                juce::MidiBuffer midi;
                juce::Random random (3);
                bool finite = true;
                double energy = 0.0;
                float peak = 0.0f;

                for (int b = 0; b < (int) (rate * 3.0) / blockSize; ++b)
                {
                    for (int ch = 0; ch < channels; ++ch)
                        for (int i = 0; i < blockSize; ++i)
                            buffer.setSample (ch, i, 0.25f * (random.nextFloat() * 2.0f - 1.0f));

                    p.processBlock (buffer, midi);

                    for (int ch = 0; ch < channels; ++ch)
                        for (int i = 0; i < blockSize; ++i)
                        {
                            const float v = buffer.getSample (ch, i);
                            finite = finite && std::isfinite (v);
                            peak = juce::jmax (peak, std::abs (v));
                            energy += (double) v * v;
                        }
                }

                check (layoutOk && finite && energy > 1.0 && peak < 4.0f,
                       juce::String (rate) + " Hz, " + juce::String (channels) + " ch, block "
                           + juce::String (blockSize) + ", peak " + juce::String (peak, 2)
                           + ", latency " + juce::String (p.getLatencySamples()));
            }

    {
        // Switched off, the plugin should return its input, delayed by the
        // reported latency.
        TempestProcessor p;
        p.apvts.getParameter ("on")->setValueNotifyingHost (0.0f);
        const int blockSize = 512;
        p.setRateAndBufferSizeDetails (48000.0, blockSize);
        p.prepareToPlay (48000.0, blockSize);
        const int latency = p.getLatencySamples();

        std::vector<float> in, out;
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        for (int b = 0; b < 20; ++b)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const float v = 0.5f * std::sin (0.05f * (float) (b * blockSize + i));
                in.push_back (v);
                buffer.setSample (0, i, v);
                buffer.setSample (1, i, v);
            }
            p.processBlock (buffer, midi);
            for (int i = 0; i < blockSize; ++i)
                out.push_back (buffer.getSample (0, i));
        }

        float worst = 0.0f;
        for (size_t i = 2048; i < in.size() - (size_t) latency; ++i)
            worst = juce::jmax (worst, std::abs (out[i + (size_t) latency] - in[i]));
        check (worst < 0.005f, "off passes the input through (worst error "
                                   + juce::String (worst, 5) + ")");
    }

    {
        TempestProcessor a, b;
        a.apvts.getParameter ("gain")->setValueNotifyingHost (0.9f);
        a.apvts.getParameter ("supply")->setValueNotifyingHost (1.0f);   // 12 V
        a.apvts.getParameter ("on")->setValueNotifyingHost (0.0f);
        juce::MemoryBlock state;
        a.getStateInformation (state);
        b.setStateInformation (state.getData(), (int) state.getSize());
        check (juce::approximatelyEqual (b.apvts.getParameter ("gain")->getValue(),
                                         a.apvts.getParameter ("gain")->getValue())
                   && b.apvts.getParameter ("supply")->getValue() > 0.9f
                   && b.apvts.getParameter ("on")->getValue() < 0.5f,
               "state round-trips");
    }

    {
        TempestProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        check (editor != nullptr && editor->getWidth() == 440, "editor constructs");

        const auto shot = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile ("build/editor.png"));
        out.deleteFile();
        if (auto stream = out.createOutputStream())
            juce::PNGImageFormat().writeImageToStream (shot, *stream);
    }

    {
        // Driven hard, so the snapshot shows the opto and clip windows lit.
        TempestProcessor p;
        p.apvts.getParameter ("gain")->setValueNotifyingHost (1.0f);
        p.apvts.getParameter ("limiter")->setValueNotifyingHost (0.3f);
        p.setRateAndBufferSizeDetails (48000.0, 512);
        p.prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        for (int b = 0; b < 40; ++b)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    buffer.setSample (ch, i, 0.5f * std::sin (0.03f * (float) (b * 512 + i)));
            p.processBlock (buffer, midi);
        }
        check (p.getGainReductionDb() > 3.0f && p.getDrive() > 1.0f,
               "meters read gain reduction " + juce::String (p.getGainReductionDb(), 1)
                   + " dB, drive " + juce::String (p.getDrive(), 2));

        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        const auto shot = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile ("build/editor-lit.png"));
        out.deleteFile();
        if (auto stream = out.createOutputStream())
            juce::PNGImageFormat().writeImageToStream (shot, *stream);
    }

    std::cout << (failures == 0 ? "All checks passed" : "CHECKS FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
