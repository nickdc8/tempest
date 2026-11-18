#pragma once

#include "PluginProcessor.h"

class TempestLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    // Sliders named "trim" are drawn as blue PCB trimmers; the rest as knobs.
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle,
                           juce::Slider&) override;
};

// The footswitch: a chrome stomp button that toggles.
class StompButton final : public juce::Button
{
public:
    StompButton() : Button ("On") { setClickingTogglesState (true); }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
};

// A PCB slide switch, for the supply.
class SlideSwitch final : public juce::Button
{
public:
    SlideSwitch() : Button ("Supply") { setClickingTogglesState (true); }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
};

// A two-pin jumper: fitted or open. For the isolator cap.
class Jumper final : public juce::Button
{
public:
    Jumper() : Button ("Isolator") { setClickingTogglesState (true); }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
};

class TempestEditor final : public juce::AudioProcessorEditor,
                            private juce::Timer
{
public:
    explicit TempestEditor (TempestProcessor&);
    ~TempestEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob
    {
        juce::Slider slider;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void timerCallback() override;
    void addKnob (Knob&, const juce::String& paramId);

    void paintEnclosure (juce::Graphics&);
    void paintBoard (juce::Graphics&);
    void paintWindow (juce::Graphics&, juce::Rectangle<float>, juce::Colour, float level);

    TempestProcessor& tempestProcessor;
    TempestLookAndFeel lookAndFeel;

    Knob gain, boost, limiter, pickup, inputLevel;
    StompButton footswitch;
    SlideSwitch supply;
    Jumper isolator;
    std::unique_ptr<ButtonAttachment> footswitchAttachment, supplyAttachment, isolatorAttachment;

    juce::Rectangle<float> enclosure, board;
    juce::Rectangle<int> ledArea, optoWindow, clipWindow;
    juce::Image finish;   // the crinkle coat, rendered once

    float led = -1.0f, opto = 0.0f, clip = 0.0f, reductionDb = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TempestEditor)
};
